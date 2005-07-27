#include <linux/kernel.h>
#include <linux/random.h>

#include "lapd.h"
#include "tei_mgmt_te.h"

struct hlist_head lapd_utme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_utme_hash_lock = RW_LOCK_UNLOCKED;

static void lapd_utme_send_to_socket(
	struct sock *sk,
	enum lapd_int_msg_type type,
	int param)
{
	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		struct sk_buff *skb;
		skb = alloc_skb(sizeof(struct lapd_internal_msg), GFP_ATOMIC);
		if (!skb) {
			lapd_printk(KERN_ERR,
				"Cannot send message %d to socket\n",
				type);

			return;
		}

		skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
		struct lapd_internal_msg *msg =
			(struct lapd_internal_msg *)skb->data;

		msg->type = type;
		msg->param = param;

		sk_add_backlog(sk, skb);
	} else {
		lapd_deliver_internal_message(sk, type, param);
	}

	bh_unlock_sock(sk);
}

/*
 * Must be called holding tme->lock
 *
 */

static inline int lapd_utme_send_tei_request(
	struct lapd_utme *tme)
{
	u16 ri;

	get_random_bytes(&ri, sizeof(ri));

	tme->tei_request_ri = ri;

	return lapd_tm_send(tme->dev, LAPD_TEI_MT_REQUEST, ri, 127);
}

/*
 * Must be called holding tme->lock
 *
 */

static inline int lapd_utme_send_tei_check_response(
	struct lapd_utme *tme, u8 tei)
{
	u16 ri;
	get_random_bytes(&ri, sizeof(ri));

	return lapd_tm_send(tme->dev, LAPD_TEI_MT_CHK_RES, ri, tme->tei);
}

/*
 * Must be called holding tme->lock
 *
 */

static inline int lapd_utme_send_tei_verify(
	struct lapd_utme *tme, u8 tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_VERIFY, 0, tme->tei);
}

/*
 *
 * DEADLOCK WARNING:
 * This function acquires tme->lock
 * Don't call it with bh_sock_lock held (in BH context) otherwise it will
 * deadlock when passing messages to the socket (timer/rcv softirq context).
 */

void lapd_utme_start_tei_request(
	struct lapd_utme *tme)
{
	// Disable BHs in order to avoid responses coming thru until we
	// have finished
	spin_lock_bh(&tme->lock);

	BUG_TRAP(tme->T202 > 0);
	BUG_TRAP(tme->N202 > 0);

	tme->retrans_cnt = 0;
	lapd_utme_reset_timer(tme, &tme->T202_timer,
		jiffies + tme->T202);

	tme->retrans_cnt++;
	tme->tei_request_pending = TRUE;
	tme->status = TEI_UNASSIGNED;

	lapd_utme_send_tei_request(tme);

	spin_unlock_bh(&tme->lock);
}

void lapd_utme_T202_timer(unsigned long data)
{
	struct lapd_utme *tme =
		(struct lapd_utme *)data;

	lapd_printk_tme(KERN_DEBUG, tme->dev,
		"tei_mgmt T202\n");

	spin_lock(&tme->lock);

	if (tme->retrans_cnt >= tme->N202) {
		tme->status = TEI_UNASSIGNED;
		tme->tei_request_pending = FALSE;
		tme->tei_request_ri = 0;

		read_lock_bh(&lapd_hash_lock);

		int i;
		for(i=0; i<ARRAY_SIZE(lapd_hash); i++) {
			struct sock *sk;
			struct hlist_node *node;

			sk_for_each(sk, node, &lapd_hash[i]) {
				if (!lapd_sk(sk)->nt_mode &&
				    lapd_sk(sk)->usr_tme == tme) {
					lapd_utme_send_to_socket(sk,
						LAPD_INT_MDL_ERROR_RESPONSE,
						0);
				}
			}
		}

		read_unlock_bh(&lapd_hash_lock);

		goto retransmit_expired;
	}

	tme->retrans_cnt++;

	lapd_utme_send_tei_request(tme);

        lapd_utme_reset_timer(tme, &tme->T202_timer,
		jiffies + tme->T202);

retransmit_expired:

	spin_unlock(&tme->lock);

	lapd_utme_put(tme);
}

/*
 *
 * DEADLOCK WARNING:
 * May acquire:
 *	read_lock_bh(&lapd_utme_hash_lock)
 *	spin_lock(&tme->lock);
 *	[Releases read_lock_bh(&lapd_utme_hash_lock)]
 *	read_lock_bh(&lapd_hash_lock);
 *	bh_sock_lock(sk);
 */

static void lapd_utme_recv_tei_assigned(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_printk(KERN_INFO, "TEI assigned\n");

	if (!tm->hdr.addr.c_r) {
		lapd_printk_tme(KERN_WARNING, skb->dev,
			"TEI assigned with C/R=0 ?\n");
	}

//A user side layer management entity receiving this identity assigned message
//shall compare the TEI value in the Ai field to its own TEI value(s) (if any) to
//see if it is already allocated if an identity request message is outstanding.
//Additionally, the TEI value in the Ai field may be compared to its TEI(s) on the
//receipt of all identity assigned messages. If there is a match, the management
//entity shall either: - initiate TEI removal; or - initiate the TEI identity
//verify procedures. TODO
	struct hlist_node *node;
	struct lapd_utme *tme;
	read_lock_bh(&lapd_utme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_utme_hash, node) {
		spin_lock(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock(&tme->lock);
			continue;
		}

		if (tme->status != TEI_UNASSIGNED &&
		    tm->body.ai == tme->tei) {

// TODO FIXME	lapd_start_tei_removal_or_initiate_tei_identify_procedures();

		} else if (tme->tei_request_pending &&
		           tm->body.ri == tme->tei_request_ri) {

			// We're not going further in the list
			read_unlock_bh(&lapd_utme_hash_lock);

			lapd_printk_tme(KERN_INFO, skb->dev,
				"TEI %u assigned\n",
				tm->body.ai);

			tme->tei_request_pending = FALSE;
			tme->tei_request_ri = 0;
			tme->tei = tm->body.ai;
			tme->status = TEI_ASSIGNED;

			lapd_utme_stop_timer(tme, &tme->T202_timer);

			read_lock_bh(&lapd_hash_lock);

			int i;
			for (i=0; i<ARRAY_SIZE(lapd_hash); i++) {
				struct sock *sk;
				struct hlist_node *node;

				sk_for_each(sk, node, &lapd_hash[i]) {
					if (!lapd_sk(sk)->nt_mode &&
					    lapd_sk(sk)->usr_tme == tme) {
						lapd_utme_send_to_socket(sk,
							LAPD_INT_MDL_ASSIGN_REQUEST,
							tme->tei);
					}
				}
			}
			read_unlock_bh(&lapd_hash_lock);

			spin_unlock(&tme->lock);

			goto tme_found;
		}

		spin_unlock(&tme->lock);
	}
	read_unlock_bh(&lapd_utme_hash_lock);

tme_found:

	return;
}

static void lapd_utme_recv_tei_denied(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_printk_tme(KERN_INFO, skb->dev,
		"TEI %u denied\n",
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		lapd_printk_tme(KERN_WARNING, skb->dev,
			"TEI denied with C/R=0 ?\n");
	}
}

/**
 *	lapd_recv_tei_check_request - Receive CHECK_REQUEST message
 *	@skb - socket buffer containing the received frame
 *
 *	Direction:	Network => User
 *
 *	Network asks if someone owns the TEI indicated in the Ri parameter,
 *	we should respond accordingly.
 */

static void lapd_utme_recv_tei_check_request(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_printk_tme(KERN_INFO, skb->dev,
		"TEI %u check request\n",
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		lapd_printk_tme(KERN_WARNING, skb->dev,
			"TEI request with C/R=0 ?\n");
	}

	struct hlist_node *node;
	struct lapd_utme *tme;
	read_lock_bh(&lapd_utme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_utme_hash, node) {
		spin_lock(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock(&tme->lock);
			continue;
		}

		if (tme->status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == tme->tei)) {
			lapd_printk_tme(KERN_INFO, skb->dev,
				"responding to TEI check request\n");

			lapd_utme_send_tei_check_response(tme, tme->tei);
		}

		spin_unlock(&tme->lock);
	}

	read_unlock_bh(&lapd_utme_hash_lock);
}

static void lapd_utme_recv_tei_remove(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_printk_tme(KERN_INFO, skb->dev,
		"TEI remove: tei=%d\n",
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		lapd_printk_tme(KERN_WARNING, skb->dev,
			"TEI request with C/R=0 ?\n");
	}

	struct hlist_node *t;
	struct lapd_utme *tme;
	read_lock_bh(&lapd_utme_hash_lock);
	hlist_for_each_entry(tme, t, &lapd_utme_hash, node) {
		spin_lock(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock(&tme->lock);
			continue;
		}

		if (tme->status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == tme->tei)) {
			lapd_printk_tme(KERN_INFO, skb->dev,
				"TEI %u removed by net request\n",
				tm->body.ai);

			tme->status = TEI_UNASSIGNED;
			tme->tei = LAPD_TEI_UNASSIGNED;

			lapd_utme_state_changed(tme);

			read_lock_bh(&lapd_hash_lock);

			int i;
			for (i=0; i<ARRAY_SIZE(lapd_hash); i++) {
				struct sock *sk;
				struct hlist_node *t2;
				sk_for_each(sk, t2, &lapd_hash[i]) {
					if (!lapd_sk(sk)->nt_mode &&
					    lapd_sk(sk)->usr_tme == tme) {
						lapd_utme_send_to_socket(sk,
							LAPD_INT_MDL_REMOVE_REQUEST,
							0);
					}
				}
			}

			read_unlock_bh(&lapd_hash_lock);

			// FIXME TODO Shall we inform the upper layer that a
			// static TEI has been removed?
		}

		spin_unlock(&tme->lock);
	}
	read_unlock_bh(&lapd_utme_hash_lock);
}

int lapd_utme_handle_frame(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	if (skb->len < sizeof(struct lapd_tei_mgmt_frame)) {
		lapd_printk_tme(KERN_ERR, skb->dev,
			"frame too small (%d bytes)\n",
			skb->len);

		return 0;
	}

	if (lapd_frame_type(tm->hdr.control) != LAPD_FRAME_TYPE_UFRAME) {
		lapd_printk_tme(KERN_ERR, skb->dev,
			"not an U-Frame (%u%u)\n",
			tm->hdr.ft2,
			tm->hdr.ft1);

		return 0;
	}

	if (lapd_uframe_function(tm->hdr.control) != LAPD_UFRAME_FUNC_UI) {
		lapd_printk_tme(KERN_ERR, skb->dev,
			"not an Unnumbered Information"
			" (%u%u)\n",
			tm->hdr.u.m3,
			tm->hdr.u.m2);

		return 0;
	}

// TODO what to do with P bit ? AFAIK it should be ignored...

	if (tm->body.entity != 0x0f) {
		lapd_printk_tme(KERN_ERR, skb->dev,
			"invalid entity %u\n",
			tm->body.entity);

		return 0;
	}

	switch (tm->body.message_type) {
	case LAPD_TEI_MT_ASSIGNED:
		lapd_utme_recv_tei_assigned(skb);
	break;

	case LAPD_TEI_MT_DENIED:
		lapd_utme_recv_tei_denied(skb);
	break;

	case LAPD_TEI_MT_CHK_REQ:
		lapd_utme_recv_tei_check_request(skb);
	break;

	case LAPD_TEI_MT_REMOVE:
		lapd_utme_recv_tei_remove(skb);
	break;

	case LAPD_TEI_MT_VERIFY:
	case LAPD_TEI_MT_CHK_RES:
	case LAPD_TEI_MT_REQUEST:
		lapd_printk_tme(KERN_INFO, skb->dev,
			"TEI management NT message (%u) in TE mode\n",
			tm->body.message_type);
	break;

	default:
		lapd_printk_tme(KERN_INFO, skb->dev,
			"unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

void lapd_utme_set_static_tei(
	struct lapd_utme *tme, u8 tei)
{
	spin_lock_bh(&tme->lock);
	tme->tei = tei;
	tme->status = TEI_ASSIGNED;
	spin_unlock_bh(&tme->lock);
}

void lapd_utme_destroy(struct lapd_utme *tme)
{
	lapd_utme_stop_timer(tme, &tme->T202_timer);

	dev_put(tme->dev);
	tme->dev = NULL;
}

struct lapd_utme *lapd_utme_alloc(struct net_device *dev)
{
	struct lapd_utme *tme;
	tme = kmalloc(sizeof(struct lapd_utme), GFP_ATOMIC);
	if (!tme) return NULL;

	memset(tme, 0x00, sizeof(struct lapd_utme));

	spin_lock_init(&tme->lock);

	tme->destroy = lapd_utme_destroy;

	atomic_set(&tme->refcnt, 1);

	dev_hold(dev);
	tme->dev = dev;

	tme->tei = -1;
	tme->tei_request_pending = FALSE;
	tme->status = TEI_UNASSIGNED;

	tme->N202 = 3;
	tme->T202 = 2 * HZ;

	init_timer(&tme->T202_timer);
	tme->T202_timer.function = lapd_utme_T202_timer;
	tme->T202_timer.data = (unsigned long)tme;

	return tme;
}

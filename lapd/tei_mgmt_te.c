#include <linux/kernel.h>
#include <linux/random.h>

#include "lapd.h"
#include "tei_mgmt_te.h"

struct hlist_head lapd_utme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_utme_hash_lock = RW_LOCK_UNLOCKED;

static inline int lapd_utme_send_tei_request(
	struct lapd_utme *tme)
{
	u16 ri;

	get_random_bytes(&ri, sizeof(ri));

	tme->tei_request_ri = ri;

	return lapd_tm_send(tme->dev, LAPD_TEI_MT_REQUEST, ri, 127);
}

static inline int lapd_utme_send_tei_check_response(
	struct lapd_utme *tme, lapd_tei_t tei)
{
	u16 ri;
	get_random_bytes(&ri, sizeof(ri));

	return lapd_tm_send(tme->dev, LAPD_TEI_MT_CHK_RES, ri, tme->tei);
}

static inline int lapd_utme_send_tei_verify(
	struct lapd_utme *tme, lapd_tei_t tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_VERIFY, 0, tme->tei);
}

void lapd_utme_start_tei_request(
	struct lapd_utme *tme)
{
	BUG_TRAP(tme->T202 > 0);
	BUG_TRAP(tme->N202 > 0);

	tme->N202_cnt = 1;
	lapd_utme_reset_timer(tme, &tme->T202_timer,
		jiffies + tme->T202);

	tme->N202_cnt++;
	tme->tei_request_pending = TRUE;
	tme->status = TEI_UNASSIGNED;

	lapd_utme_send_tei_request(tme);
}

void lapd_utme_T202_timer(unsigned long data)
{
	struct lapd_utme *tme =
		(struct lapd_utme *)data;

	printk(KERN_DEBUG "lapd: tei_mgmt T202\n");

	if (tme->N202_cnt > tme->N202) {
		tme->status = TEI_UNASSIGNED;
		tme->tei_request_pending = FALSE;
		tme->tei_request_ri = 0;

		lapd_utme_put(tme);
		return;
	}

	tme->N202_cnt++;

	lapd_utme_send_tei_request(tme);

        lapd_utme_reset_timer(tme, &tme->T202_timer,
		jiffies + tme->T202);

	lapd_utme_put(tme);
}

static void lapd_utme_recv_tei_assigned(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI assigned\n",
		skb->dev->name);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI assigned with C/R=0 ?\n",
			skb->dev->name);
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
		if (tme->dev != skb->dev) continue;

		if (tme->status != TEI_UNASSIGNED &&
		    tm->body.ai == tme->tei) {

// TODO FIXME	lapd_start_tei_removal_or_initiate_tei_identify_procedures();

		} else if (tme->tei_request_pending &&
		           tm->body.ri == tme->tei_request_ri) {

			printk(KERN_INFO
				"lapd: tei_mgmt: TEI %u assigned\n",
				tm->body.ai);

			tme->tei_request_pending = FALSE;
			tme->tei_request_ri = 0;
			tme->tei = tm->body.ai;
			tme->status = TEI_ASSIGNED;

			lapd_utme_stop_timer(tme, &tme->T202_timer);

			// Notify state change
			lapd_utme_state_changed(tme);

			break;
		}
	}

	read_unlock_bh(&lapd_utme_hash_lock);
}

static void lapd_utme_recv_tei_denied(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI %u denied\n",
		skb->dev->name,
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI denied with C/R=0 ?\n",
			skb->dev->name);
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
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI %u check request\n",
		skb->dev->name,
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	struct hlist_node *node;
	struct lapd_utme *tme;
	read_lock_bh(&lapd_utme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_utme_hash, node) {
		if (tme->dev != skb->dev) continue;

		if (tme->status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == tme->tei)) {
			printk(KERN_INFO
				"lapd: tei_mgmt: "
				"responding to TEI check request\n");

			lapd_utme_send_tei_check_response(tme, tme->tei);
		}
	}

	read_unlock_bh(&lapd_utme_hash_lock);
}

static void lapd_utme_recv_tei_remove(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI remove: tei=%d\n",
		skb->dev->name,
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	struct hlist_node *node;
	struct lapd_utme *tme;
	read_lock_bh(&lapd_utme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_utme_hash, node) {
		if (tme->dev != skb->dev) continue;

		if (tme->status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == tme->tei)) {
			printk(KERN_INFO
				"lapd: tei_mgmt: "
				"TEI %u removed by net request\n",
				tm->body.ai);

			tme->status = TEI_UNASSIGNED;
			tme->tei = LAPD_TEI_UNASSIGNED;

			lapd_utme_state_changed(tme);

			lapd_utme_start_tei_request(tme);

			// FIXME TODO Discard U and I queues

			// Shall we inform the upper layer that a static TEI has
			// been removed?
		}
	}

	read_unlock_bh(&lapd_utme_hash_lock);
}

int lapd_utme_handle_frame(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	if (skb->len < sizeof(struct lapd_tei_mgmt_frame)) {
		printk(KERN_ERR
			"lapd: tei management: frame too small (%d bytes)\n",
			skb->len);

		return 0;
	}

	if (lapd_frame_type(tm->hdr.control) != UFRAME) {
		printk(KERN_ERR
			"lapd: tei management: not an U-Frame (%u%u)\n",
			tm->hdr.ft2,
			tm->hdr.ft1);

		return 0;
	}

	if (lapd_uframe_function(tm->hdr.control) != UI) {
		printk(KERN_ERR
			"lapd: tei management: not an Unnumbered Information"
			" (%u%u)\n",
			tm->hdr.u.m3,
			tm->hdr.u.m2);

		return 0;
	}

// TODO what to do with P bit ?
// FIXME answer with F bit! :)

	if (tm->body.entity != 0x0f) {
		printk(KERN_ERR
			"lapd: tei management: invalid entity %u\n",
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
	default:
		printk(KERN_INFO
			"lapd: tei_mgmt: "
			"unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

int lapd_utme_wait_for_tei_assignment(struct lapd_utme *tme)
{
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(&tme->waitq, &wait,
			TASK_INTERRUPTIBLE);

		if (tme->status == TEI_ASSIGNED)
			break;
		//lapd_utme_unlock(tme);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		//lapd_utme_lock(tme);

		if (tme->status == TEI_ASSIGNED)
			break;

		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}

		if (!timeout) {
			err = -EAGAIN;
			break;
		}
	}

	finish_wait(&tme->waitq, &wait);

	return err;
}

void lapd_utme_set_static_tei(
	struct lapd_utme *tme, lapd_tei_t tei)
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

	init_waitqueue_head(&tme->waitq);

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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "tei_mgmt.h"

struct hlist_head lapd_net_tme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_net_tme_hash_lock = RW_LOCK_UNLOCKED;

static inline int lapd_tme_te_send_tei_request(
	struct lapd_tei_mgmt_entity *tme)
{
	int ri;

	get_random_bytes(&ri, sizeof(ri));

	tme->te.tei_request_ri = ri;

	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_REQUEST, ri, 127);
}

static inline int lapd_tme_te_send_tei_check_response(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	u16 ri;
	get_random_bytes(&ri, sizeof(ri));

	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_CHK_RES, ri, tei);
}

static inline int lapd_tme_te_send_tei_verify(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_VERIFY, 0, tei);
}

static void lapd_tme_te_tes_del_by_tei(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	struct lapd_te *te, *n;
	write_lock_bh(&tme->nt.tes_lock);

	list_for_each_entry_safe(te, n, &tme->nt.tes, hash_list) {
		if (te->tei == tei) {
			list_del(&te->hash_list);
			kfree(te);
		}
	}

	write_unlock_bh(&tme->nt.tes_lock);
}

void lapd_tme_te_start_tei_request(
	struct lapd_tei_mgmt_entity *tme)
{
	BUG_TRAP(tme->T202 > 0);
	BUG_TRAP(tme->N202 > 0);

	tme->N202_cnt = 1;
	sk_reset_timer(tme->sk, &tme->T202_timer,
		jiffies + tme->T202);

	tme->N202_cnt++;
	tme->tei_request_pending = TRUE;
	tme->te.status = TEI_UNASSIGNED;

	lapd_send_tei_request(tme->sk);
}

void lapd_tme_te_T202_timer(unsigned long data)
{
	struct lapd_tei_mgmt_entity *tme = (struct lapd_tei_mgmt_entity *)data;

	printk(KERN_DEBUG "lapd: tei_mgmt T202\n");

	if (tme->N202_cnt > tme->N202) {
		tme->te.status = TEI_UNASSIGNED;
		tme->te.tei_request_pending = FALSE;
		tme->te.tei_request_ri = 0;

		sock_put(sk);
		return;
	}

	tme->N202_cnt++;

	lapd_send_tei_request(tme);

        sk_reset_timer(tme->sk, &tme->T202_timer,
		jiffies + tme->T202);

	sock_put(tme->sk);
}

static void lapd_tme_te_recv_tei_assigned(struct sk_buff *skb)
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
	struct lapd_tei_mgmt_entity *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_hash, node) {
		if (!tme->dev->flags & IFF_ALLMULTI) continue;
		if (tme->dev != skb->dev) continue;

		if (tme->te.status != TEI_UNASSIGNED &&
		    tm->body.ai == tme->te.tei) {

// TODO FIXME	lapd_start_tei_removal_or_initiate_tei_identify_procedures();

		} else if (tme->te.tei_request_pending &&
		           tm->body.ri == tme->te.tei_request_ri) {

			printk(KERN_INFO
				"lapd: tei_mgmt: TEI %u assigned\n",
				tm->body.ai);

			tme->te.tei_request_pending = FALSE;
			tme->te.tei_request_ri = 0;
			tme->te.tei = tm->body.ai;
			tme->te.status = TEI_ASSIGNED;

			sk_stop_timer(sk, &tme->T202_timer);

			// Notify state change
			if (waitqueue_active(&tme->waitq))
				wake_up_interruptible_all(&tme->waitq);

			break;
		}
	}

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_tme_te_recv_tei_denied(struct sk_buff *skb)
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

static void lapd_tme_te_recv_tei_check_request(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	struct sock *sk = NULL;
	struct hlist_node *node;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI %u check request\n",
		skb->dev->name,
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		if (lo->te.status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == lo->te.tei)) {
			printk(KERN_INFO
				"lapd: tei_mgmt: "
				"responding to TEI check request\n");

			lapd_send_tei_check_response(sk, lo->te.tei);
		}
	}

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_tme_te_recv_tei_remove(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	struct sock *sk = NULL;
	struct hlist_node *node;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI remove: tei=%d\n",
		skb->dev->name,
		tm->body.ai);

	if (!tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		if (lo->te.status != TEI_UNASSIGNED &&
		    (tm->body.ai == LAPD_BROADCAST_TEI ||
		     tm->body.ai == lo->te.tei)) {
			printk(KERN_INFO
				"lapd: tei_mgmt: TEI %u removed by net request\n",
				tm->body.ai);

			lo->te.status = TEI_UNASSIGNED;
			lo->te.tei = -1;

			lapd_start_tei_request(sk);

			// FIXME TODO Discard U and I queues

			// Shall we inform the upper layer that a static TEI has
			// been removed?
		}
	}

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_tme_te_recv_tei_verify(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	struct sock *sk = NULL;
	struct hlist_node *node;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI verify received: tei=%d\n",
		skb->dev->name,
		tm->body.ai);

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI verify with C/R=1 ?\n",
			skb->dev->name);
	}

	if (tm->body.ai == LAPD_BROADCAST_TEI) {
		printk(KERN_INFO
			"lapd: tei_mgmt: received invalid verify"
			" request with tei=127\n");
		return;
	}

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (!lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		printk(KERN_INFO
			"lapd: tei_mgmt: starting TEI check\n");

		lapd_start_tei_check(sk, tm->body.ai);
	}

	read_unlock_bh(&lapd_hash_lock);
}

int lapd_tme_te_handle_frame(struct sk_buff *skb)
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
		lapd_recv_tei_assigned(skb);
	break;

	case LAPD_TEI_MT_DENIED:
		lapd_recv_tei_denied(skb);
	break;

	case LAPD_TEI_MT_CHK_REQ:
		lapd_recv_tei_check_request(skb);
	break;

	case LAPD_TEI_MT_REMOVE:
		lapd_recv_tei_remove(skb);
	break;

	case LAPD_TEI_MT_VERIFY:
	case LAPD_TEI_MT_CHK_RES:
	case LAPD_TEI_MT_REQUEST:
	default:
		printk(KERN_INFO
			"lapd: tei_mgmt: unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

static int lapd_wait_for_tei_assignment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

/*
	BUG_TRAP(lo->te.tei_request_pending);

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		release_sock(sk);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		if (lo->te.status == TEI_ASSIGNED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			printk(KERN_ERR "lapd: Failure in assigning TEI\n");

			err = -EAGAIN;
			break;
		}

		err = sock_error(sk);
		if (err)
			break;
	}

	finish_wait(sk->sk_sleep, &wait);
*/
	return err;
}

void lapd_usr_tme_set_static_tei(
	struct lapd_usr_tei_mgmt_entity *tme, int tei)
{
	tme->tei = tei;
	tme->status = TEI_ASSIGNED;
}

struct lapd_usr_tei_mgmt_entity *lapd_usr_tei_mgmt_entity_alloc()
{
	struct lapd_usr_tei_mgmt_entity *tme;
	tme = kmalloc(sizeof(struct lapd_usr_tei_mgmt_entity), GFP_ATOMIC);
	if (!tme) return NULL;

	atomic_set(&tme->refcnt, 1);

	tme->tei = -1;
	tme->tei_request_pending = FALSE;
	tme->status = TEI_UNASSIGNED;

	lo->tei_mgmt.N202 = 3;
	lo->tei_mgmt.T202 = 2 * HZ;

	init_timer(&tme->T202_timer);
	tme->T202_timer.function = lapd_usr_tei_mgmt_T202_timer;
	tme->T202_timer.data = (unsigned long)sk;
}

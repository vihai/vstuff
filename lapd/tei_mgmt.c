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

static inline int lapd_send_tei_mgmt(struct sock *sk, u8 message_type,
	int c_r, u16 ri, u8 ai)
{
	int err;

	struct sk_buff *skb =
		lapd_prepare_uframe(sk, LAPD_SAPI_TEI_MGMT,
			LAPD_BROADCAST_TEI, UI,
			sizeof(struct lapd_tei_mgmt_body), &err);

	struct lapd_tei_mgmt_body *tm =
		 (struct lapd_tei_mgmt_body *)skb_put(skb,
			 sizeof(struct lapd_tei_mgmt_body));

	tm->entity = LAPD_TEI_ENTITY;
	tm->message_type = message_type;
	tm->ri = ri;
	tm->ai = ai;
	tm->ai_ext = 1;

	return lapd_send_frame(skb);
}

static inline int lapd_send_tei_request(struct sock *sk)
{
	int ri;

	get_random_bytes(&ri, sizeof(ri));

	struct lapd_opt *lo = lapd_sk(sk);
	lo->te.tei_request_ri = ri;

	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_REQUEST, 0, ri, 127);
}

static inline int lapd_send_tei_assigned(struct sock *sk, u16 ri, int tei)
{
	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_ASSIGNED, 1, ri, tei);
}

static inline int lapd_send_tei_denied(struct sock *sk, u16 ri, int tei)
{
	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_DENIED, 1, ri, tei);
}

static inline int lapd_send_tei_check_request(struct sock *sk, int tei)
{
	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_CHK_REQ, 1, 0, tei);
}

static inline int lapd_send_tei_check_response(struct sock *sk, int tei)
{
	u16 ri;
	get_random_bytes(&ri, sizeof(ri));

	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_CHK_RES, 0, ri, tei);
}

static inline int lapd_send_tei_remove(struct sock *sk, int tei)
{
	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_REMOVE, 1, 0, tei);
}

static inline int lapd_send_tei_verify(struct sock *sk, int tei)
{
	return lapd_send_tei_mgmt(sk, LAPD_TEI_MT_VERIFY, 0, 0, tei);
}

static void lapd_tes_del_by_tei(struct lapd_opt *lo, int tei)
{
	struct lapd_te *te, *n;
	write_lock_bh(&lo->nt.tes_lock);

	list_for_each_entry_safe(te, n, &lo->nt.tes, hash_list) {
		if (te->tei == tei) {
			list_del(&te->hash_list);
			kfree(te);
		}
	}

	write_unlock_bh(&lo->nt.tes_lock);
}

static void lapd_start_tei_check(struct sock *sk, int tei)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_TRAP(lo->tei_mgmt_sap.T201 > 0);

	sk_reset_timer(sk, &lo->tei_mgmt_sap.T201_timer,
		jiffies + lo->tei_mgmt_sap.T201);

	lo->nt.tei_check_outstanding = TRUE;
	lo->nt.tei_check_count = 0;
	lo->nt.tei_check_responses[0] = 0;
	lo->nt.tei_check_responses[1] = 0;
	lo->nt.tei_check_tei = tei;

	lapd_send_tei_check_request(sk, tei);
}

void lapd_start_tei_request(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_TRAP(lo->tei_mgmt_sap.T202 > 0);
	BUG_TRAP(lo->tei_mgmt_sap.N202 > 0);

	lo->tei_mgmt_sap.N202_cnt = 1;
	sk_reset_timer(sk, &lo->tei_mgmt_sap.T202_timer,
		jiffies + lo->tei_mgmt_sap.T202);

	lo->tei_mgmt_sap.N202_cnt++;
	lo->te.tei_request_pending = TRUE;
	lo->te.status = TEI_UNASSIGNED;

	lapd_send_tei_request(sk);
}

void lapd_tei_mgmt_T201_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: tei_mgmt T201\n");

	// TODO FIXME Locking of lo->nt ?????

	BUG_TRAP(lo->nt.tei_check_outstanding);

	if (lo->nt.tei_check_count == 0) {
		// End of first T201 window
		if (lo->nt.tei_check_responses[0] < 2) {
			// Multiple TEIs assigned

			lo->nt.tei_check_outstanding = FALSE;
			lapd_send_tei_remove(sk, lo->nt.tei_check_tei);
		} else {
			// We need to try a second time

			lo->nt.tei_check_count++;

			lapd_send_tei_check_request(sk, lo->nt.tei_check_tei);
		        sk_reset_timer(sk, &lo->tei_mgmt_sap.T201_timer,
				jiffies + lo->tei_mgmt_sap.T201);
		}
	} else {
		lo->nt.tei_check_outstanding = FALSE;

		// End of second T201 window
		if (lo->nt.tei_check_responses[0] == 0 &&
		    lo->nt.tei_check_responses[1] == 0) {
			// TEI is unused

			lapd_tes_del_by_tei(lo, lo->nt.tei_check_tei);
		} else if (lo->nt.tei_check_responses[1] == 1 ||
		           lo->nt.tei_check_responses[1] == 1) {
			// TEI in use
		} else {
			// Multiple TEIs assigned

			lapd_send_tei_remove(sk, lo->nt.tei_check_tei);
		}
	}

	sock_put(sk);
}

void lapd_tei_mgmt_T202_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: tei_mgmt T202\n");

	if (lo->tei_mgmt_sap.N202_cnt > lo->tei_mgmt_sap.N202) {
		lo->te.status = TEI_UNASSIGNED;
		lo->te.tei_request_pending = FALSE;
		lo->te.tei_request_ri = 0;

		sock_put(sk);
		return;
	}

	lo->tei_mgmt_sap.N202_cnt++;

	lapd_send_tei_request(sk);

        sk_reset_timer(sk, &lo->tei_mgmt_sap.T202_timer,
		jiffies + lo->tei_mgmt_sap.T202);
	sock_put(sk);
}

static int lapd_find_available_tei(struct lapd_opt *lo)
{
	int i;
	for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {

		lo->nt.cur_dyn_tei++;
		if(lo->nt.cur_dyn_tei > LAPD_MAX_DYN_TEI)
			lo->nt.cur_dyn_tei = LAPD_MAX_DYN_TEI;

		read_lock_bh(&lo->nt.tes_lock);
		struct lapd_te *te;
		list_for_each_entry(te, &lo->nt.tes, hash_list) {
			if (te->tei == lo->nt.cur_dyn_tei) {
				read_unlock_bh(&lo->nt.tes_lock);
				continue;
			}
		}
		read_unlock_bh(&lo->nt.tes_lock);

		return lo->nt.cur_dyn_tei;
	}

	return -1;
}

static void lapd_recv_tei_request(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
	struct sock *sk = NULL;
	struct hlist_node *node;

	printk(KERN_INFO
		"lapd: tei_mgmt: TEI request\n");

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=1 ?\n",
			skb->dev->name);
	}

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (!lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		if (/*tm->body.ai >= LAPD_MIN_STA_TEI && // always true */
		    tm->body.ai <= LAPD_MAX_STA_TEI) {
			break;
		} else if (tm->body.ai >= LAPD_MIN_DYN_TEI &&
		           tm->body.ai <= LAPD_MAX_DYN_TEI) {
			lapd_send_tei_denied(sk, tm->body.ri, tm->body.ai);
			break;
		}

		printk(KERN_INFO
			"lapd: tei_mgmt: TEI %u requested\n",
			tm->body.ai);

		int tei = lapd_find_available_tei(lo);

		struct lapd_te *new_te =
			kmalloc(sizeof(struct lapd_te), GFP_KERNEL);

		// FIXME could this be a deadlock?
		write_lock_bh(&lo->nt.tes_lock);
		list_add(&new_te->hash_list, &lo->nt.tes);
		write_unlock_bh(&lo->nt.tes_lock);

		lapd_send_tei_assigned(sk, tm->body.ri, tei);

		goto found;
	}

//	lapd_start_tei_check(sk, tm->body.ai);

	found:

	read_unlock_bh(&lapd_hash_lock);
}


static void lapd_recv_tei_assigned(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
	struct sock *sk = NULL;
	struct hlist_node *node;

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
	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		if (lo->te.status != TEI_UNASSIGNED &&
		    tm->body.ai == lo->te.tei) {

// TODO FIXME	lapd_start_tei_removal_or_initiate_tei_identify_procedures();

		} else if (lo->te.tei_request_pending &&
		           tm->body.ri == lo->te.tei_request_ri) {

			printk(KERN_INFO
				"lapd: tei_mgmt: TEI %u assigned\n",
				tm->body.ai);

			lo->te.tei_request_pending = FALSE;
			lo->te.tei_request_ri = 0;
			lo->te.tei = tm->body.ai;
			lo->te.status = TEI_ASSIGNED;

			lo->q931_sap.status = LINK_CONNECTION_RELEASED;

			sk_stop_timer(sk, &lo->tei_mgmt_sap.T202_timer);

			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_state_change(sk);

			break;
		}
	}

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_recv_tei_denied(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
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

/*
 * Send CHECK_REQUEST message
 * Sent by: network
 */

static void lapd_recv_tei_check_request(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
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


static void lapd_recv_tei_check_response(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
	struct sock *sk = NULL;
	struct hlist_node *node;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI check response\n",
		skb->dev->name);

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (!lo->nt_mode) continue;
		if (lo->dev != skb->dev) continue;

		if (!lo->nt.tei_check_outstanding) {
			printk(KERN_WARNING
				"lapd: tei_mgmt: "
				"unexpected/late check response\n");
			break;
		}

		lo->nt.tei_check_responses[lo->nt.tei_check_count]++;
	}

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_recv_tei_remove(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
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

static void lapd_recv_tei_verify(struct sk_buff *skb,
	struct lapd_tei_mgmt_frame *tm)
{
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

int lapd_handle_tei_mgmt(struct sk_buff *skb)
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

	if (tm->body.entity != 0x0f) {
		printk(KERN_ERR
			"lapd: tei management: invalid entity %u\n",
			tm->body.entity);

		return 0;
	}

	switch (tm->body.message_type) {
	case LAPD_TEI_MT_ASSIGNED:
		lapd_recv_tei_assigned(skb, tm);
	break;

	case LAPD_TEI_MT_DENIED:
		lapd_recv_tei_denied(skb, tm);
	break;

	case LAPD_TEI_MT_CHK_REQ:
		lapd_recv_tei_check_request(skb, tm);
	break;

	case LAPD_TEI_MT_CHK_RES:
		lapd_recv_tei_check_response(skb, tm);
	break;

	case LAPD_TEI_MT_REMOVE:
		lapd_recv_tei_remove(skb, tm);
	break;

	case LAPD_TEI_MT_VERIFY:
		lapd_recv_tei_verify(skb, tm);
	break;

	case LAPD_TEI_MT_REQUEST:
		lapd_recv_tei_request(skb, tm);
	break;

	default:
		printk(KERN_INFO
			"lapd: tei_mgmt: unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

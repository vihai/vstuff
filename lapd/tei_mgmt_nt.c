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

static inline int lapd_tme_nt_send_tei_assigned(
	struct lapd_tei_mgmt_entity *tme, u16 ri, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_ASSIGNED, ri, tei);
}

static inline int lapd_tme_nt_send_tei_denied(
	struct lapd_tei_mgmt_entity *tme, u16 ri, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_DENIED, ri, tei);
}

static inline int lapd_tme_nt_send_tei_check_request(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_CHK_REQ, 0, tei);
}

static inline int lapd_tme_nt_send_tei_remove(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_REMOVE, 0, tei);
}

static inline int lapd_tme_nt_send_tei_verify(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	return lapd_send_tei_mgmt(tme, LAPD_TEI_MT_VERIFY, 0, tei);
}

static void lapd_tme_nt_tes_del_by_tei(
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

static void lapd_tme_nt_start_tei_check(
	struct lapd_tei_mgmt_entity *tme, int tei)
{
	BUG_TRAP(tme->tei_mgmt_sap.T201 > 0);

	sk_reset_timer(tme->sk, &tme->tei_mgmt_sap.T201_timer,
		jiffies + tme->tei_mgmt_sap.T201);

	tme->nt.tei_check_outstanding = TRUE;
	tme->nt.tei_check_count = 0;
	tme->nt.tei_check_responses[0] = 0;
	tme->nt.tei_check_responses[1] = 0;
	tme->nt.tei_check_tei = tei;

	lapd_send_tei_check_request(tme->sk, tei);
}

void lapd_tme_nt_tei_mgmt_T201_timer(unsigned long data)
{
	struct lapd_tei_mgmt_entity *tme = (struct lapd_tei_mgmt_entity *)data;

	printk(KERN_DEBUG "lapd: tei_mgmt T201\n");

	// TODO FIXME Locking of lo->nt ?????

	BUG_TRAP(tme->nt.tei_check_outstanding);

	if (tme->nt.tei_check_count == 0) {
		// End of first T201 window
		if (tme->nt.tei_check_responses[0] < 2) {
			// Multiple TEIs assigned

			tme->nt.tei_check_outstanding = FALSE;
			lapd_send_tei_remove(tme->sk, tme->nt.tei_check_tei);
		} else {
			// We need to try a second time

			tme->nt.tei_check_count++;

			lapd_send_tei_check_request(tme->sk, tme->nt.tei_check_tei);
		        sk_reset_timer(tme->sk, &tme->T201_timer,
				jiffies + tme->T201);
		}
	} else {
		lo->nt.tei_check_outstanding = FALSE;

		// End of second T201 window
		if (tme->nt.tei_check_responses[0] == 0 &&
		    tme->nt.tei_check_responses[1] == 0) {
			// TEI is unused

			lapd_tes_del_by_tei(tme, tme->nt.tei_check_tei);
		} else if (tme->nt.tei_check_responses[1] == 1 ||
		           tme->nt.tei_check_responses[1] == 1) {
			// TEI in use
		} else {
			// Multiple TEIs assigned

			lapd_send_tei_remove(tme->sk, tme->nt.tei_check_tei);
		}
	}

	// TODO Uhm.... ?
	sock_put(tme->sk);
}

static int lapd_tme_nt_find_available_tei(
	struct lapd_tei_mgmt_entity *tme)
{
	int i;
	for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {

		tme->nt.cur_dyn_tei++;
		if(tme->nt.cur_dyn_tei > LAPD_MAX_DYN_TEI)
			tme->nt.cur_dyn_tei = LAPD_MAX_DYN_TEI;

		read_lock_bh(&tme->nt.tes_lock);
		struct lapd_te *te;
		list_for_each_entry(te, &tme->nt.tes, hash_list) {
			if (te->tei == tme->nt.cur_dyn_tei) {
				read_unlock_bh(&tme->nt.tes_lock);
				continue;
			}
		}
		read_unlock_bh(&tme->nt.tes_lock);

		return tme->nt.cur_dyn_tei;
	}

	return -1;
}

static void lapd_tme_nt_recv_tei_request(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: TEI request\n");

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=1 ?\n",
			skb->dev->name);
	}

	struct lapd_tei_mgmt_entity *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_hash, node) {
		if (!tme->dev->flags & IFF_ALLMULTI) continue;
		if (tme->dev != skb->dev) continue;

		if (/*tm->body.ai >= LAPD_MIN_STA_TEI && // always true */
		    tm->body.ai <= LAPD_MAX_STA_TEI) {
			break;
		} else if (tm->body.ai >= LAPD_MIN_DYN_TEI &&
		           tm->body.ai <= LAPD_MAX_DYN_TEI) {
			lapd_send_tei_denied(tme, tm->body.ri, tm->body.ai);
			break;
		}

		int tei = lapd_tme_nt_find_available_tei(tme);

		struct lapd_te *new_te =
			kmalloc(sizeof(struct lapd_te), GFP_KERNEL);

		// FIXME could this be a deadlock?
		write_lock_bh(&tme->nt.tes_lock);
		list_add(&new_te->hash_list, &tme->nt.tes);
		write_unlock_bh(&tme->nt.tes_lock);

		lapd_send_tei_assigned(tme, tm->body.ri, tei);

		goto found;
	}

//	lapd_start_tei_check(tme, tm->body.ai);

	found:

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_tme_nt_recv_tei_check_response(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

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

static void lapd_tme_nt_recv_tei_verify(struct sk_buff *skb)
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

int lapd_tme_nt_handle_frame(struct sk_buff *skb)
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
	case LAPD_TEI_MT_CHK_RES:
		lapd_tme_nt_recv_tei_check_response(skb);
	break;

	case LAPD_TEI_MT_VERIFY:
		lapd_tme_nt_recv_tei_verify(skb);
	break;

	case LAPD_TEI_MT_REQUEST:
		lapd_tme_nt_recv_tei_request(skb);
	break;

	case LAPD_TEI_MT_REMOVE:
	case LAPD_TEI_MT_ASSIGNED:
	case LAPD_TEI_MT_DENIED:
	case LAPD_TEI_MT_CHK_REQ:
	default:
		printk(KERN_INFO
			"lapd: tei_mgmt: unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

struct lapd_net_tei_mgmt_entity *lapd_net_tei_mgmt_entity_alloc()
{
	struct lapd_net_tei_mgmt_entity *tme;
	tme = kmalloc(sizeof(struct lapd_net_tei_mgmt_entity), GFP_ATOMIC);
	if (!tme) return NULL;

	atomic_set(&tme->refcnt, 1);

//	INIT_LIST_HEAD(&tme->tes);

	tme->tes_lock = RW_LOCK_UNLOCKED;
	tme->cur_dyn_tei = LAPD_MIN_DYN_TEI;
	tme->tei_check_outstanding = FALSE;

	int i;
	for (i=0; i<LAPD_MAX_NUM_TEIS; i++) {
		tme->teis[i].tei = -1;
		tme->teis[i].status = TEI_UNASSIGNED;
	}

	tme->T201 = 1 * HZ;

	init_timer(&tme->T201_timer);
	tme->T201_timer.function = lapd_net_tei_mgmt_T201_timer;
	tme->T201_timer.data = (unsigned long)sk;
}

/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/skbuff.h>

#include "lapd.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"

struct hlist_head lapd_ntme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_ntme_hash_lock = RW_LOCK_UNLOCKED;

/*
 * Must be called holding tme->lock
 */

static inline int lapd_ntme_send_tei_assigned(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_ASSIGNED, ri, tei);
}

/*
 * Must be called holding tme->lock
 */

static inline int lapd_ntme_send_tei_denied(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_DENIED, ri, tei);
}

/*
 * Must be called holding tme->lock
 */

static inline int lapd_ntme_send_tei_check_request(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_CHK_REQ, 0, tei);
}

/*
 * Must be called holding tme->lock
 */

static inline int lapd_ntme_send_tei_remove(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_REMOVE, 0, tei);
}

/*
 * Must be called holding tme->lock
 */

static inline int lapd_ntme_send_tei_verify(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_VERIFY, 0, tei);
}

static void _lapd_ntme_start_tei_check(
	struct lapd_ntme *tme, int tei)
{
	WARN_ON(tme->T201 == 0);
	WARN_ON(tme->tei_check_outstanding);

	lapd_ntme_reset_timer(tme, &tme->T201_timer,
		jiffies + tme->T201);

	tme->tei_check_outstanding = TRUE;
	tme->tei_check_count = 0;
	tme->tei_check_responses[0] = 0;
	tme->tei_check_responses[1] = 0;
	tme->tei_check_tei = tei;

	lapd_ntme_send_tei_check_request(tme, tei);
}

void lapd_ntme_start_tei_check(
	struct lapd_ntme *tme, int tei)
{
	spin_lock_bh(&tme->lock);
	_lapd_ntme_start_tei_check(tme, tei);
	spin_unlock_bh(&tme->lock);
}

void lapd_ntme_T201_timer(unsigned long data)
{
	struct lapd_ntme *tme =
		(struct lapd_ntme *)data;

	lapd_msg(KERN_DEBUG, "tei_mgmt T201\n");

	spin_lock_bh(&tme->lock);

	BUG_TRAP(tme->tei_check_outstanding);

	if (tme->tei_check_count == 0) {
		// End of first T201 window
		if (tme->tei_check_responses[0] > 1) {
			// Multiple TEIs assigned

			tme->tei_check_outstanding = FALSE;
			lapd_ntme_send_tei_remove(tme, tme->tei_check_tei);
		} else {
			// We need to try a second time

			tme->tei_check_count++;

			lapd_ntme_send_tei_check_request(tme, tme->tei_check_tei);
		        lapd_ntme_reset_timer(tme, &tme->T201_timer,
				jiffies + tme->T201);
		}
	} else {
		tme->tei_check_outstanding = FALSE;

		// End of second T201 window
		if (tme->tei_check_responses[0] == 0 &&
		    tme->tei_check_responses[1] == 0) {
			// TEI is unused

			tme->teis[tme->tei_check_tei] = LAPD_TEI_UNASSIGNED;
		} else if (tme->tei_check_responses[1] == 1 ||
		           tme->tei_check_responses[1] == 1) {
			// TEI in use
		} else {
			// Multiple TEIs assigned

			lapd_ntme_send_tei_remove(tme, tme->tei_check_tei);
		}
	}

	spin_unlock_bh(&tme->lock);

	lapd_ntme_put(tme);
}

static void lapd_ntme_handle_tei_request(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_msg_tme(KERN_INFO, skb->dev,
		"TEI request\n");

	if (tm->hdr.addr.c_r) {
		lapd_msg_tme(KERN_WARNING, skb->dev,
			"TEI request with C/R=1 ?\n");
	}

	struct lapd_ntme *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {

		spin_lock_bh(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		if (/*tm->body.ai >= LAPD_MIN_STA_TEI && // always true */
		    tm->body.ai <= LAPD_MAX_STA_TEI) {
			spin_unlock_bh(&tme->lock);
			goto found;
		} else if (tm->body.ai >= LAPD_MIN_DYN_TEI &&
		           tm->body.ai <= LAPD_MAX_DYN_TEI) {
			lapd_ntme_send_tei_denied(tme,
					tm->body.ri, tm->body.ai);
			spin_unlock_bh(&tme->lock);
			goto found;
		}

		u8 tei = LAPD_TEI_UNASSIGNED;

		int i;
		for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {

			tme->cur_dyn_tei++;

			if(tme->cur_dyn_tei > LAPD_MAX_DYN_TEI)
				tme->cur_dyn_tei = LAPD_MAX_DYN_TEI;

			if (tme->teis[tme->cur_dyn_tei - LAPD_MIN_DYN_TEI] ==
						LAPD_TEI_UNASSIGNED) {
				tei = tme->cur_dyn_tei;

				break;
			}
		}

		lapd_ntme_send_tei_assigned(tme, tm->body.ri, tei);

		spin_unlock_bh(&tme->lock);
		goto found;
	}

	lapd_msg_tme(KERN_WARNING, skb->dev,
		"Missing TEI management entity\n");

	found:

	read_unlock_bh(&lapd_ntme_hash_lock);
}

static void lapd_ntme_handle_tei_check_response(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_msg_dev(skb->dev, KERN_INFO,
		"TEI check response\n");

	if (tm->hdr.addr.c_r) {
		lapd_msg_dev(skb->dev, KERN_WARNING,
			"TEI request with C/R=0 ?\n");
	}

	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {

		spin_lock_bh(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		if (!tme->tei_check_outstanding) {
			lapd_msg_dev(skb->dev, KERN_WARNING,
				"unexpected/late check response\n");
			spin_unlock_bh(&tme->lock);
			break;
		}

		tme->tei_check_responses[tme->tei_check_count]++;

		spin_unlock_bh(&tme->lock);
	}

	read_unlock_bh(&lapd_ntme_hash_lock);
}

static void lapd_ntme_handle_tei_verify(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	lapd_msg_tme(KERN_INFO, skb->dev,
		"TEI verify received: tei=%d\n",
		tm->body.ai);

	if (tm->hdr.addr.c_r) {
		lapd_msg_dev(skb->dev, KERN_WARNING,
			"TEI verify with C/R=1 ?\n");
	}

	if (tm->body.ai == LAPD_BROADCAST_TEI) {
		lapd_msg_dev(skb->dev, KERN_INFO,
			"received invalid verify"
			" request with tei=127\n");
		return;
	}

	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {

		spin_lock_bh(&tme->lock);

		if (tme->dev != skb->dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		lapd_msg_dev(skb->dev, KERN_INFO,
			"starting TEI check\n");

		// We're not going any futher in the list
		read_unlock_bh(&lapd_ntme_hash_lock);

		_lapd_ntme_start_tei_check(tme, tm->body.ai);

		spin_unlock_bh(&tme->lock);

		break;
	}
}

int lapd_ntme_handle_frame(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->mac.raw;

	if (skb->len < sizeof(struct lapd_tei_mgmt_frame)) {
		lapd_msg_dev(skb->dev, KERN_ERR,
			"frame too small (%d bytes)\n",
			skb->len);

		return 0;
	}

	if (lapd_frame_type(tm->hdr.control) != LAPD_FRAME_TYPE_UFRAME) {
		lapd_msg_dev(skb->dev, KERN_ERR,
			"not an U-Frame (%u%u)\n",
			tm->hdr.ft2,
			tm->hdr.ft1);

		return 0;
	}

	if (lapd_uframe_function(tm->hdr.control) != LAPD_UFRAME_FUNC_UI) {
		lapd_msg_dev(skb->dev, KERN_ERR,
			"not an Unnumbered Information"
			" (%u%u)\n",
			tm->hdr.u.m3,
			tm->hdr.u.m2);

		return 0;
	}

// TODO what to do with P bit ?
// FIXME answer with F bit! :)

	if (tm->body.entity != 0x0f) {
		lapd_msg_dev(skb->dev, KERN_ERR,
			"invalid entity %u\n",
			tm->body.entity);

		return 0;
	}

	switch (tm->body.message_type) {
	case LAPD_TEI_MT_CHK_RES:
		lapd_ntme_handle_tei_check_response(skb);
	break;

	case LAPD_TEI_MT_VERIFY:
		lapd_ntme_handle_tei_verify(skb);
	break;

	case LAPD_TEI_MT_REQUEST:
		lapd_ntme_handle_tei_request(skb);
	break;

	case LAPD_TEI_MT_REMOVE:
	case LAPD_TEI_MT_ASSIGNED:
	case LAPD_TEI_MT_DENIED:
	case LAPD_TEI_MT_CHK_REQ:
		lapd_msg_dev(skb->dev, KERN_INFO,
			"TEI Management TE message (%u) in NT mode\n",
			tm->body.message_type);
	break;

	default:
		lapd_msg_dev(skb->dev, KERN_INFO,
			"unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

void lapd_ntme_get(
	struct lapd_ntme *tme)
{
	atomic_inc(&tme->refcnt);
}

void lapd_ntme_put(
	struct lapd_ntme *tme)
{
	if (atomic_dec_and_test(&tme->refcnt)) {
		lapd_ntme_stop_timer(tme, &tme->T201_timer);

		dev_put(tme->dev);
		tme->dev = NULL;

		kfree(tme);
	}
}


struct lapd_ntme *lapd_ntme_alloc(struct net_device *dev)
{
	struct lapd_ntme *tme;
	tme = kmalloc(sizeof(struct lapd_ntme), GFP_ATOMIC);
	if (!tme)
		return NULL;

	memset(tme, 0, sizeof(*tme));

	atomic_set(&tme->refcnt, 1);

	spin_lock_init(&tme->lock);

	dev_hold(dev);
	tme->dev = dev;

	tme->cur_dyn_tei = LAPD_MIN_DYN_TEI;
	tme->tei_check_outstanding = FALSE;

	int i;
	for (i=0; i<LAPD_NUM_DYN_TEIS; i++)
		tme->teis[i] = LAPD_TEI_UNASSIGNED;

	tme->T201 = 1 * HZ;

	init_timer(&tme->T201_timer);
	tme->T201_timer.function = lapd_ntme_T201_timer;
	tme->T201_timer.data = (unsigned long)tme;

	return tme;
}

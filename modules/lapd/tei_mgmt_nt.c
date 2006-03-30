/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/skbuff.h>

#include "lapd.h"
#include "device.h"
#include "output.h"
#include "tei_mgmt_nt.h"

struct hlist_head lapd_ntme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_ntme_hash_lock = RW_LOCK_UNLOCKED;

/*
 * Must be called holding tme->lock
 */

static int lapd_ntme_send_tei_assigned(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_ASSIGNED, ri, tei);
}

/*
 * Must be called holding tme->lock
 */

static int lapd_ntme_send_tei_denied(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_DENIED, ri, tei);
}

/*
 * Must be called holding tme->lock
 */

static int lapd_ntme_send_tei_check_request(
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

void lapd_ntme_tc_check_first(
	struct lapd_ntme_tei_check *tc,
	u8 tei)
{
	if(tc->responses[tei][0] > 1)
		lapd_ntme_send_tei_remove(tc->tme, tei);
}

void lapd_ntme_tc_check_second(
	struct lapd_ntme_tei_check *tc,
	u8 tei)
{
	struct lapd_ntme *tme = tc->tme;

	if(tc->responses[tei][0] == 0 &&
	   tc->responses[tei][1] == 0) {
		/* TEI is unused */
		if (tei >= LAPD_MIN_DYN_TEI &&
		    tei <= LAPD_MAX_DYN_TEI) {
			/* Close corresponding sockets TODO FIXME */
			tme->teis[tei - LAPD_MIN_DYN_TEI] = FALSE;
		}

	} else if (tc->responses[tei][0] <= 1 &&
		   tc->responses[tei][1] <= 1) {
		/* TEI in use */
	} else {
		/* Multiple TEIs assigned */

		lapd_msg_tme(tme, KERN_INFO,
			"Removing TEI %d due to"
			" multiple TEI assignment\n",
			tei);

		lapd_ntme_send_tei_remove(tme, tei);
	}
}

void lapd_ntme_tc_T201_timer(unsigned long data)
{
	struct lapd_ntme_tei_check *tc =
		(struct lapd_ntme_tei_check *)data;
	struct lapd_ntme *tme = tc->tme;

	spin_lock_bh(&tme->lock);

	if (tc->count == 0) {
		/* End of first T201 window */
		if (tc->tei == LAPD_BROADCAST_TEI) {
			int i;
			for (i=0; i<ARRAY_SIZE(tc->responses); i++)
				lapd_ntme_tc_check_first(tc, i);

		} else {
			/* Multiple TEIs assigned */
			lapd_ntme_tc_check_first(tc, tc->tei);

			list_del(&tc->node);
			kfree(tc);

			goto finished;
		}

		/* We need to try a second time */
		tc->count++;
		
		lapd_ntme_send_tei_check_request(tme, tc->tei);
		lapd_ntme_tc_reset_timer(tc, &tc->T201_timer,
			jiffies + tme->T201);
	} else {
		/* End of second T201 window */
		if (tc->tei == LAPD_BROADCAST_TEI) {
			int i;
			for (i=0; i<ARRAY_SIZE(tc->responses); i++)
				lapd_ntme_tc_check_second(tc, i);

		} else {
			lapd_ntme_tc_check_second(tc, tc->tei);
			
		}

		list_del(&tc->node);
		kfree(tc);
	}

finished:
	spin_unlock_bh(&tme->lock);

	lapd_ntme_put(tme);
}

static void _lapd_ntme_start_tei_check(
	struct lapd_ntme *tme, int tei)
{
	/* Check for duplicates */
	struct lapd_ntme_tei_check *tc;
	list_for_each_entry(tc, &tme->tei_checks, node) {
		if (tc->tei == tei)
			return;
	}

	tc = kmalloc(sizeof(*tc), GFP_ATOMIC);
	if (!tc)
		return;

	memset(tc, 0, sizeof(*tc));

	tc->tme = tme;

	init_timer(&tc->T201_timer);
	tc->T201_timer.function = lapd_ntme_tc_T201_timer;
	tc->T201_timer.data = (unsigned long)tc;

	lapd_ntme_tc_reset_timer(tc, &tc->T201_timer,
		jiffies + tme->T201);

	tc->count = 0;
	tc->tei = tei;

	list_add_tail(&tc->node, &tme->tei_checks);

	lapd_ntme_send_tei_check_request(tme, tei);
}

void lapd_ntme_start_tei_check(
	struct lapd_ntme *tme, int tei)
{
	spin_lock_bh(&tme->lock);
	_lapd_ntme_start_tei_check(tme, tei);
	spin_unlock_bh(&tme->lock);
}

static void lapd_ntme_handle_tei_request(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->data;

	lapd_debug_dev(dev, "TEI request\n");

	if (tm->hdr.addr.c_r) {
		lapd_msg_dev(dev, KERN_WARNING,
			"TEI request with C/R=1 ?\n");
	}

	if (dev->mode == LAPD_INTF_MODE_POINT_TO_POINT) {
		lapd_debug_dev(dev,
			"Rejecting TEI request on a P2P interface\n");

		lapd_tm_send(dev, LAPD_TEI_MT_DENIED,
			tm->tm_hdr.ri, tm->ai.value);
		return;
	}

	{
	struct lapd_ntme *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {
		u8 tei;
		int tei_found;
		int i;

		spin_lock_bh(&tme->lock);

		if (tme->dev != dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		if (/*tm->ai.value >= LAPD_MIN_STA_TEI && // always true */
		    tm->ai.value <= LAPD_MAX_STA_TEI) {
			spin_unlock_bh(&tme->lock);
			goto found;
		} else if (tm->ai.value >= LAPD_MIN_DYN_TEI &&
		           tm->ai.value <= LAPD_MAX_DYN_TEI) {
			lapd_ntme_send_tei_denied(tme,
					tm->tm_hdr.ri, tm->ai.value);
			spin_unlock_bh(&tme->lock);
			goto found;
		}

		tei = LAPD_TEI_UNASSIGNED;

		tei_found = FALSE;
		for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {

			tme->cur_dyn_tei++;

			if(tme->cur_dyn_tei > LAPD_MAX_DYN_TEI)
				tme->cur_dyn_tei = LAPD_MIN_DYN_TEI;

			if (!tme->teis[tme->cur_dyn_tei - LAPD_MIN_DYN_TEI]) {

				tei = tme->cur_dyn_tei;

				tme->teis[tme->cur_dyn_tei - LAPD_MIN_DYN_TEI] =
					TRUE;

				tei_found = TRUE;

				break;
			}
		}

		if (tei_found) {
			lapd_msg_dev(dev, KERN_INFO, "Assigning TEI %d\n", tei);
			lapd_ntme_send_tei_assigned(tme, tm->tm_hdr.ri, tei);
		} else {
			lapd_msg_dev(dev, KERN_NOTICE,
				"No more available TEIs, "
				"starting check procedure\n");

			lapd_ntme_send_tei_denied(tme, tm->tm_hdr.ri,
						tm->ai.value);

			for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {
				_lapd_ntme_start_tei_check(tme,
						LAPD_MIN_DYN_TEI + i);
			}
		}

		spin_unlock_bh(&tme->lock);
		goto found;
	}
	}

	lapd_msg_dev(dev, KERN_WARNING,
		"Missing TEI management entity\n");

	found:

	read_unlock_bh(&lapd_ntme_hash_lock);
}

static void lapd_ntme_handle_tei_check_response(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_tei_mgmt_frame_noai *tm =
		(struct lapd_tei_mgmt_frame_noai *)skb->data;
	u8 teis[128];
	int nteis = 0;

	if (tm->hdr.addr.c_r) {
		lapd_msg_dev(dev, KERN_WARNING,
			"TEI request with C/R=0 ?\n");
	}

	{
	struct lapd_tei_mgmt_ai *tm_ai;
	do {
		tm_ai = (struct lapd_tei_mgmt_ai *)(skb->data + nteis);

		teis[nteis++] = tm_ai->value & 0x7F;
	} while(tm_ai->ext == 0);
	}

	{
	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {

		spin_lock_bh(&tme->lock);

		if (tme->dev != dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		{
		struct lapd_ntme_tei_check *tc;
		list_for_each_entry(tc, &tme->tei_checks, node) {
			int i;
			for (i=0; i<nteis; i++) {
				if (tc->tei == LAPD_BROADCAST_TEI ||
				    tc->tei == teis[i])
					tc->responses[teis[i]][tc->count]++;
			}
		}
		}

		spin_unlock_bh(&tme->lock);
	}
	}

	read_unlock_bh(&lapd_ntme_hash_lock);
}

static void lapd_ntme_handle_tei_verify(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->data;

	lapd_msg_dev(dev, KERN_INFO,
		"TEI verify received: tei=%d\n",
		tm->ai.value);

	if (tm->hdr.addr.c_r) {
		lapd_msg_dev(dev, KERN_WARNING,
			"TEI verify with C/R=1 ?\n");
	}

	if (tm->ai.value == LAPD_BROADCAST_TEI) {
		lapd_msg_dev(dev, KERN_INFO,
			"received invalid verify"
			" request with tei=127\n");
		return;
	}

	{
	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {

		spin_lock_bh(&tme->lock);

		if (tme->dev != dev) {
			spin_unlock_bh(&tme->lock);
			continue;
		}

		lapd_msg_dev(dev, KERN_INFO,
			"starting TEI check\n");

		/* We're not going any futher in the list */
		read_unlock_bh(&lapd_ntme_hash_lock);

		_lapd_ntme_start_tei_check(tme, tm->ai.value);

		spin_unlock_bh(&tme->lock);

		break;
	}
	}
}

int lapd_ntme_handle_frame(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->data;

	if (skb->len < sizeof(struct lapd_tei_mgmt_frame)) {
		lapd_msg_dev(dev, KERN_ERR,
			"frame too small (%d bytes)\n",
			skb->len);

		return 0;
	}

	if (lapd_frame_type(tm->hdr.control) != LAPD_FRAME_TYPE_UFRAME) {
		lapd_msg_dev(dev, KERN_ERR,
			"not an U-Frame (%u%u)\n",
			tm->hdr.ft2,
			tm->hdr.ft1);

		return 0;
	}

	if (lapd_uframe_function(tm->hdr.control) != LAPD_UFRAME_FUNC_UI) {
		lapd_msg_dev(dev, KERN_ERR,
			"not an Unnumbered Information"
			" (%u%u)\n",
			tm->hdr.u.m3,
			tm->hdr.u.m2);

		return 0;
	}

	if (tm->tm_hdr.entity != 0x0f) {
		lapd_msg_dev(dev, KERN_ERR,
			"invalid entity %u\n",
			tm->tm_hdr.entity);

		return 0;
	}

	switch (tm->tm_hdr.message_type) {
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
		lapd_msg_dev(dev, KERN_INFO,
			"TEI Management TE message (%u) in NT mode\n",
			tm->tm_hdr.message_type);
	break;

	default:
		lapd_msg_dev(dev, KERN_INFO,
			"unknown/unimplemented message_type %u\n",
			tm->tm_hdr.message_type);
	}

	return 0;
}

void lapd_ntme_audit()
{
	struct lapd_ntme *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {
		spin_lock_bh(&tme->lock);
		_lapd_ntme_start_tei_check(tme, LAPD_BROADCAST_TEI);
		spin_unlock_bh(&tme->lock);
	}
	read_unlock_bh(&lapd_ntme_hash_lock);
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
		struct lapd_ntme_tei_check *tc, *tpos;
		list_for_each_entry_safe(tc, tpos, &tme->tei_checks, node) {
			lapd_ntme_tc_stop_timer(tc, &tc->T201_timer);
			
			list_del(&tc->node);
			kfree(tc);
		}

		lapd_dev_put(tme->dev);
		tme->dev = NULL;

		kfree(tme);
	}
}

struct lapd_ntme *lapd_ntme_alloc(struct lapd_device *dev)
{
	int i;
	struct lapd_ntme *tme;
	u8 random_byte;

	tme = kmalloc(sizeof(struct lapd_ntme), GFP_ATOMIC);
	if (!tme)
		return NULL;

	memset(tme, 0, sizeof(*tme));

	atomic_set(&tme->refcnt, 1);

	spin_lock_init(&tme->lock);

	lapd_dev_get(dev);
	tme->dev = dev;

	get_random_bytes(&random_byte, sizeof(random_byte));
	tme->cur_dyn_tei = (random_byte % LAPD_NUM_DYN_TEIS) + LAPD_MIN_DYN_TEI;

	for (i=0; i<LAPD_NUM_DYN_TEIS; i++)
		tme->teis[i] = FALSE;

	tme->T201 = 1 * HZ;

	INIT_LIST_HEAD(&tme->tei_checks);

	return tme;
}

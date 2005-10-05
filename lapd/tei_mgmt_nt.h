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

#ifndef _TEI_MGMT_NT_H
#define _TEI_MGMT_NT_H

#include <asm/atomic.h>
#include <linux/spinlock.h>

#include "tei_mgmt.h"

extern struct hlist_head lapd_ntme_hash;

struct lapd_ntme
{
	struct hlist_node node;

	atomic_t refcnt;

	struct net_device *dev;
	wait_queue_head_t waitq;

	spinlock_t lock;

	// TEI Management SAP Parameters
	int T201;
	//---------

	struct timer_list T201_timer;

	int cur_dyn_tei;
//	struct list_head tes;

	int tei_check_outstanding;
	int tei_check_count;
	int tei_check_responses[2];
	u8 tei_check_tei;

	u8 teis[LAPD_NUM_DYN_TEIS];
};

struct lapd_ntme *lapd_ntme_alloc(struct net_device *net);


void lapd_ntme_get(struct lapd_ntme *tme);
void lapd_ntme_put(struct lapd_ntme *tme);

static inline void lapd_ntme_reset_timer(
	struct lapd_ntme *tme,
	struct timer_list *timer,
	unsigned long expires)
{
	if (!mod_timer(timer, expires))
		lapd_ntme_get(tme);
}

static inline void lapd_ntme_stop_timer(
	struct lapd_ntme *tme,
	struct timer_list *timer)
{
	if (timer_pending(timer) && del_timer(timer))
		lapd_ntme_put(tme);
}

void lapd_ntme_set_static_tei(
	struct lapd_ntme *tme, int tei);
int lapd_ntme_handle_frame(struct sk_buff *skb);

#endif

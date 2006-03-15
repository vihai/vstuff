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

#ifndef _TEI_MGMT_TE_H
#define _TEI_MGMT_TE_H

#include <linux/spinlock.h>
#include <asm/atomic.h>

#include "device.h"
#include "tei_mgmt.h"

extern struct hlist_head lapd_utme_hash;
extern rwlock_t lapd_utme_hash_lock;

struct lapd_utme
{
	struct hlist_node node;

	atomic_t refcnt;

	struct lapd_device *dev;

	spinlock_t lock;

	// TEI Management SAP Parameters
	int T202;
	int N202;
	//---------

	struct timer_list T202_timer;
	int retrans_cnt;

	enum lapd_tei_state state;

	u8 tei;
	u16 tei_request_ri;
	int tei_request_pending;
};

struct lapd_utme *lapd_utme_get(struct lapd_utme *tme);
void lapd_utme_put(struct lapd_utme *tme);

extern void lapd_utme_set_static_tei(
	struct lapd_utme *tme, u8 tei);

static inline void lapd_utme_reset_timer(
	struct lapd_utme *tme,
	struct timer_list *timer,
	unsigned long expires)
{
	if (!mod_timer(timer, expires))
		lapd_utme_get(tme);
}

static inline void lapd_utme_stop_timer(
	struct lapd_utme *tme,
	struct timer_list *timer)
{
	if (timer_pending(timer) && del_timer(timer))
		lapd_utme_put(tme);
}

struct lapd_utme *lapd_utme_alloc(struct lapd_device *dev);
int lapd_utme_handle_frame(struct sk_buff *skb);
void lapd_utme_mdl_assign_indication(struct lapd_utme *tme);
void lapd_utme_tei_remove(struct lapd_utme *tme);
void lapd_utme_assign_static_tei(struct lapd_utme *tme, u8 tei);
void lapd_utme_remove_tei(struct lapd_device *dev, u8 tei);

#endif

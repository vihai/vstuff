/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_CXC_H
#define _VISDN_CXC_H

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

#define VISDN_CXC_HASHBITS 8

struct visdn_cxc
{
	struct hlist_head entries_hash[1 << VISDN_CXC_HASHBITS];
	spinlock_t lock;
};

struct visdn_cxc_entry
{
	struct hlist_node node;
	struct rcu_head rcu;

	struct visdn_chan *src;
	struct visdn_chan *dst;
};

extern struct visdn_cxc visdn_cxc;

extern int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);
extern void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);
extern struct visdn_chan *visdn_cxc_get_by_src(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);
extern void visdn_cxc_init(struct visdn_cxc *cxc);

#endif

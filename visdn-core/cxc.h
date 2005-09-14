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

extern struct subsystem visdn_tdm_subsys;

struct visdn_cxc;
struct visdn_cxc_ops
{
	struct module *owner;

	void (*release)(struct visdn_cxc *cxc);
};

struct visdn_cxc
{
	struct subsystem subsys;

	struct visdn_cxc_ops *ops;

	const char *name;

	struct list_head channels;
	struct hlist_head connections_hash[1 << VISDN_CXC_HASHBITS];
	spinlock_t lock;
};

struct visdn_cxc_connection
{
	struct hlist_node node;
	struct rcu_head rcu;

	struct visdn_chan *src;
	struct visdn_chan *dst;
};

extern struct visdn_chan *visdn_cxc_search_chan(
	struct visdn_cxc *cxc,
	const char *chanid);
extern int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);
extern void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);

extern int visdn_cxc_connect(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);
extern void visdn_cxc_disconnect(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);

extern struct visdn_chan *visdn_cxc_get_by_src(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan);

static inline struct visdn_cxc *visdn_cxc_get(
	struct visdn_cxc *cxc)
{
	return cxc ? container_of(subsys_get(&cxc->subsys),
			struct visdn_cxc, subsys) : NULL;
}

static inline void visdn_cxc_put(
	struct visdn_cxc *cxc)
{
	if (cxc)
		subsys_put(&cxc->subsys);
}

struct visdn_cxc_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_cxc *cxc,
		struct visdn_cxc_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_cxc *cxc,
		struct visdn_cxc_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_CXC_ATTR(_name,_mode,_show,_store) \
	struct visdn_cxc_attribute visdn_cxc_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)


extern void visdn_cxc_init(struct visdn_cxc *cxc);
extern int visdn_cxc_register(struct visdn_cxc *cxc);
extern void visdn_cxc_unregister(struct visdn_cxc *cxc);

int visdn_cxc_create_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr);
void visdn_cxc_remove_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr);

extern int visdn_cxc_modinit(void);
extern void visdn_cxc_modexit(void);

#endif

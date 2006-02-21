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

#ifndef _VISDN_CHAN_H
#define _VISDN_CHAN_H

#include <kernel_config.h>

#define VISDN_CHANID_SIZE	32

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/sysfs.h>

#include "leg.h"

struct visdn_chan_class
{
	const char *name;
};

struct visdn_chan;
struct visdn_chan_ops
{
	struct module *owner;

	void (*release)(struct visdn_chan *chan);

	int (*open)(struct visdn_chan *chan);
	int (*close)(struct visdn_chan *chan);
};

#define to_visdn_chan(obj) container_of(obj, struct visdn_chan, kobj)

struct visdn_chan
{
	struct kobject kobj;

	struct hlist_node node;

	int id;

	struct visdn_chan_class *chan_class;
	struct visdn_port *port;

	char name[KOBJ_NAME_LEN];

	void *driver_data;

	struct visdn_chan_ops *ops;

	struct semaphore sem;

	unsigned long state;

	int bitrate;

	int write_priority;

	struct visdn_leg leg_a;
	struct visdn_leg leg_b;

	struct visdn_router_node router_node;
};

struct visdn_chan_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_chan *chan,
		struct visdn_chan_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_chan *chan,
		struct visdn_chan_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_CHAN_ATTR(_name,_mode,_show,_store) \
	struct visdn_chan_attribute visdn_chan_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern struct subsystem visdn_channels_subsys;

extern int visdn_chan_create_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *entry);

extern void visdn_chan_remove_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute * attr);

extern struct visdn_chan *visdn_chan_get_by_id(int id);

int visdn_chan_modinit(void);
void visdn_chan_modexit(void);

static inline struct visdn_chan *visdn_chan_get(
	struct visdn_chan *chan)
{
	return chan ? to_visdn_chan(kobject_get(&chan->kobj)) : NULL;
}

static inline void visdn_chan_put(
	struct visdn_chan *chan)
{
	if (chan)
		kobject_put(&chan->kobj);
}

static inline void visdn_chan_lock(
	struct visdn_chan *chan)
{
	down(&chan->sem);
}

static inline int visdn_chan_lock_interruptible(
	struct visdn_chan *chan)
{
	return down_interruptible(&chan->sem);
}

static inline void visdn_chan_unlock(
	struct visdn_chan *chan)
{
	up(&chan->sem);
}

static inline int visdn_chan_refcount(
	struct visdn_chan *chan)
{
#ifdef HAVE_KREF
	return atomic_read(&chan->kobj.kref.refcount);
#else
	return atomic_read(&chan->kobj.refcount);
#endif
}

enum visdn_chan_state
{
	VISDN_CHAN_STATE_CONNECTED	= 0,
	VISDN_CHAN_STATE_OPEN		= 1,
};

extern int visdn_chan_lock2(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

extern int visdn_chan_lock2_interruptible(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

extern void visdn_chan_init(struct visdn_chan *chan);

extern int visdn_chan_register(struct visdn_chan *chan);
extern void visdn_chan_unregister(struct visdn_chan *chan);

extern int visdn_chan_enable(struct visdn_chan *chan);
extern int visdn_chan_disable(struct visdn_chan *chan);

#endif

#endif

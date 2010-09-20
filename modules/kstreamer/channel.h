/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_CHANNEL_H
#define _KS_CHANNEL_H

enum ks_chan_attribute_type
{
	KS_CHANATTR_ID = 1,
	KS_CHANATTR_PATH,
	KS_CHANATTR_FROM,
	KS_CHANATTR_TO,
};

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

#include <kernel_config.h>

#include "netlink.h"

extern struct kset *ks_chans_kset;

struct ks_chan;

struct ks_chan_ops
{
	struct module *owner;

	void (*release)(struct ks_chan *chan);

	int (*connect)(struct ks_chan *chan);
	void (*disconnect)(struct ks_chan *chan);

	int (*open)(struct ks_chan *chan);
	void (*close)(struct ks_chan *chan);

	int (*start)(struct ks_chan *chan);
	void (*stop)(struct ks_chan *chan);

	void (*stimulus)(struct ks_chan *chan);

	int (*get_attr_count)(struct ks_chan *chan);
	int (*get_attr)(struct ks_chan *chan, int index, u16 *type,
			void *buf, int *len);
	int (*set_attr)(struct ks_chan *chan, u16 type, void *buf,int len);
};

struct ks_chan
{
	struct kobject kobj;
	struct list_head node;

	int id;

	struct ks_chan_ops *ops;
	struct ks_duplex *duplex;

	struct ks_node *from;
	void *from_ops;

	struct ks_node *to;
	void *to_ops;

	int mtu;

	struct ks_pipeline *pipeline;
	spinlock_t pipeline_lock;

	struct list_head pipeline_entry;

	void *driver_data;
};

#define to_ks_chan(obj) container_of(obj, struct ks_chan, kobj)

struct ks_chan_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct ks_chan *chan,
		struct ks_chan_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct ks_chan *chan,
		struct ks_chan_attribute *attr,
		const char *buf,
		size_t count);
};

#define KS_CHAN_ATTR(_name,_mode,_show,_store) \
	struct ks_chan_attribute ks_chan_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern int ks_chan_create_file(
	struct ks_chan *chan,
	struct ks_chan_attribute *entry);

extern void ks_chan_remove_file(
	struct ks_chan *chan,
	struct ks_chan_attribute * attr);

extern struct ks_chan *ks_chan_create(
	struct ks_chan *chan,
	struct ks_chan_ops *ops,
	const char *name,
	struct ks_duplex *duplex,
	struct kobject *parent,
	struct ks_node *from,
	struct ks_node *to);
extern void ks_chan_destroy(struct ks_chan *chan);

extern int ks_chan_register(struct ks_chan *chan);
extern void ks_chan_unregister(struct ks_chan *chan);

struct ks_chan *ks_chan_get_by_id(int id);
struct ks_chan *ks_chan_get_by_nlid(struct nlmsghdr *nlh);

int ks_chan_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh);
int ks_chan_cmd_set(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh);

static inline struct ks_chan *ks_chan_get(struct ks_chan *chan)
{
	return chan ? to_ks_chan(kobject_get(&chan->kobj)) : NULL;
}

static inline void ks_chan_put(struct ks_chan *chan)
{
#if 0
	ks_debug(3, "ks_chan_put() refcnt=%d\n",
			atomic_read(&chan->kobj.kref.refcount) - 1);
#endif

	if (chan)
		kobject_put(&chan->kobj);
}

int ks_chan_modinit(void);
void ks_chan_modexit(void);

#endif

#endif

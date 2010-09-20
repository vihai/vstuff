/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_NODE_H
#define _KS_NODE_H

enum ks_node_attribute_type
{
	KS_NODEATTR_ID = 1,
	KS_NODEATTR_PATH,
};

#ifdef __KERNEL__

#include <linux/kobject.h>

#include "netlink.h"

extern struct kset *ks_nodes_kset;

struct ks_chan;
struct ks_pipeline;

struct ks_node;
struct ks_node_ops
{
	struct module *owner;
	atomic_t *refcnt;

	void (*release)(struct ks_node *node);

	int (*connect)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	void (*disconnect)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	int (*open)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	void (*close)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	int (*start)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	void (*stop)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);

	void (*stimulus)(
		struct ks_node *node,
		struct ks_chan *link1,
		struct ks_chan *link2);
};

struct ks_duplex;
struct ks_node
{
	struct kobject kobj;
	struct list_head node;

	int id;

	struct ks_node_ops *ops;

	void *driver_data;
};

struct ks_node_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct ks_node *node,
		struct ks_node_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct ks_node *node,
		struct ks_node_attribute *attr,
		const char *buf,
		size_t count);
};

#define to_ks_node(obj) container_of(obj, struct ks_node, kobj)

#define KS_NODE_ATTR(_name,_mode,_show,_store) \
	struct ks_node_attribute ks_node_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern struct ks_node *ks_node_create(
	struct ks_node *node,
	struct ks_node_ops *ops,
	const char *name,
	struct kobject *parent);
extern void ks_node_destroy(struct ks_node *node);

int ks_node_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh);

extern int ks_node_register(struct ks_node *node);
extern void ks_node_unregister(struct ks_node *node);

//struct ks_node *ks_node_get_by_path(const char *path);

extern int ks_node_create_file(
	struct ks_node *node,
	struct ks_node_attribute *attr);
extern void ks_node_remove_file(
	struct ks_node *node,
	struct ks_node_attribute *attr);

extern int ks_node_modinit(void);
extern void ks_node_modexit(void);

static inline struct ks_node *ks_node_get(struct ks_node *node)
{
	return node ? to_ks_node(kobject_get(&node->kobj)) : NULL;
}

static inline void ks_node_put(struct ks_node *node)
{
	kobject_put(&node->kobj);
}

#endif

#endif

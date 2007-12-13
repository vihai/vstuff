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

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/netlink.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "pipeline.h"
#include "duplex.h"
#include "netlink.h"

struct kset ks_nodes_kset;

static struct list_head ks_nodes_list = LIST_HEAD_INIT(ks_nodes_list);
static rwlock_t ks_nodes_list_lock = RW_LOCK_UNLOCKED;

struct ks_node *_ks_node_search_by_id(int id)
{
	struct ks_node *node;
	list_for_each_entry(node, &ks_nodes_list, node) {
		if (node->id == id)
			return node;
	}

	return NULL;
}

struct ks_node *ks_node_get_by_id(int id)
{
	struct ks_node *node;

	read_lock(&ks_nodes_list_lock);
	node = ks_node_get(_ks_node_search_by_id(id));
	read_unlock(&ks_nodes_list_lock);

	return node;
}

static int _ks_node_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing node ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_ks_node_search_by_id(cur_id))
			return cur_id;
	}
}

#define to_ks_node_attr(_attr) \
	container_of(_attr, struct ks_node_attribute, attr)

static ssize_t ks_node_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct ks_node_attribute *ks_node_attr =
					to_ks_node_attr(attr);
	struct ks_node *ks_node = to_ks_node(kobj);
	ssize_t err;

	if (ks_node_attr->show)
		err = ks_node_attr->show(ks_node, ks_node_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t ks_node_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct ks_node_attribute *ks_node_attr =
					to_ks_node_attr(attr);
	struct ks_node *ks_node = to_ks_node(kobj);
	ssize_t err;

	if (ks_node_attr->store)
		err = ks_node_attr->store(ks_node, ks_node_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops ks_node_sysfs_ops = {
	.show   = ks_node_attr_show,
	.store  = ks_node_attr_store,
};

static void ks_node_release(struct kobject *kobj)
{
	struct ks_node *node = to_ks_node(kobj);

	ks_debug(3, "ks_node_release()\n");

	if (node->ops->release)
		node->ops->release(node);
	else
		kfree(node);
}

static struct attribute *ks_node_default_attrs[] =
{
	NULL,
};

static struct kobj_type ks_node_ktype = {
	.release	= ks_node_release,
	.sysfs_ops	= &ks_node_sysfs_ops,
	.default_attrs	= ks_node_default_attrs,
};

struct ks_node *ks_node_create(
	struct ks_node *node,
	struct ks_node_ops *ops,
	const char *name,
	struct kobject *parent)
{
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!name);

	if (!node) {
		node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return NULL;
	}

	memset(node, 0, sizeof(*node));

	node->kobj.kset = &ks_nodes_kset;
	node->kobj.parent = parent;

	kobject_init(&node->kobj);
	kobject_set_name(&node->kobj, "%s", name);

	node->ops = ops;

	return node;
}
EXPORT_SYMBOL(ks_node_create);

int ks_node_write_to_nlmsg(
	struct ks_node *node,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	u32 pid, u32 seq, u16 flags)
{
	struct nlmsghdr *nlh;
	unsigned char *oldtail;
	int err = -ENOBUFS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	oldtail = skb->tail;
#else
	oldtail = skb_tail_pointer(skb);
#endif

	nlh = NLMSG_PUT(skb, pid, seq, message_type, 0);
	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_NODEATTR_ID, &node->id,
						sizeof(node->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_NODE_DEL) {
		err = ks_netlink_put_attr_path(skb, KS_NODEATTR_PATH,
						&node->kobj);
		if (err < 0)
			goto err_put_attr;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	nlh->nlmsg_len = skb->tail - oldtail;
#else
	nlh->nlmsg_len = skb_tail_pointer(skb) - oldtail;
#endif

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

static int ks_node_mcast_send(
	struct ks_node *node,
	struct ks_netlink_state *state,
	enum ks_netlink_message_type message_type)
{
	int err;

retry:
	err = ks_netlink_mcast_need_skb(state);
	if (err < 0)
		return err;

	err = ks_node_write_to_nlmsg(node, state->mcast_skb, message_type,
					0,  state->mcast_seqnum++, 0);
	if (err < 0) {
		ks_netlink_mcast_flush(state);
		goto retry;
	}

	ks_netlink_mcast_flush(state);

	return 0;
}

int ks_node_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	int err;
	struct ks_node *node;
	int cnt = 1;

	ks_netlink_send_ack(state, nlh, NLM_F_MULTI);
  
	/* No need to read_lock(&ks_nodes_list_lock); because we are also
	 * protected by ks_topology_lock semaphore.
	 */

	list_for_each_entry(node, &ks_nodes_list, node) {

retry:
		ks_netlink_need_skb(state);
		if (!state->out_skb)
			return -ENOMEM;

		err = ks_node_write_to_nlmsg(node,
					state->out_skb,
					KS_NETLINK_NODE_NEW,
					nlh->nlmsg_pid,
					nlh->nlmsg_seq + cnt,
					NLM_F_MULTI);
		if (err < 0) {
			ks_netlink_flush(state);
			goto retry;
		}

		cnt++;
	}

	ks_netlink_send_done(state, nlh, cnt);

	return 0;
}

int ks_node_register_no_topology_lock(struct ks_node *node)
{
	int err;

	BUG_ON(!node);

	write_lock(&ks_nodes_list_lock);
	node->id = _ks_node_new_id();
	list_add_tail(&ks_node_get(node)->node, &ks_nodes_list);
	write_unlock(&ks_nodes_list_lock);

	err = kobject_add(&node->kobj);
	if (err < 0)
		goto err_kobject_add;

	ks_node_mcast_send(node, &ks_netlink_state, KS_NETLINK_NODE_NEW);

	return 0;

	kobject_del(&node->kobj);
err_kobject_add:
	write_lock(&ks_nodes_list_lock);
	list_del(&node->node);
	write_unlock(&ks_nodes_list_lock);
	ks_node_put(node);

	return err;
}

int ks_node_register(struct ks_node *node)
{
	int err;

	ks_topology_lock();
	err = ks_node_register_no_topology_lock(node);
	ks_topology_unlock();

	return err;
}
EXPORT_SYMBOL(ks_node_register);

void ks_node_unregister_no_topology_lock(struct ks_node *node)
{
	kobject_del(&node->kobj);

	write_lock(&ks_nodes_list_lock);
	list_del(&node->node);
	write_unlock(&ks_nodes_list_lock);
	ks_node_put(node);

	ks_node_mcast_send(node, &ks_netlink_state, KS_NETLINK_NODE_DEL);
}

void ks_node_unregister(struct ks_node *node)
{
	ks_topology_lock();
	ks_node_unregister_no_topology_lock(node);
	ks_topology_unlock();
}
EXPORT_SYMBOL(ks_node_unregister);

void ks_node_destroy(struct ks_node *node)
{
	ks_kobj_waitref(&node->kobj);
	ks_node_put(node);
}
EXPORT_SYMBOL(ks_node_destroy);

int ks_node_create_file(
	struct ks_node *node,
	struct ks_node_attribute *attr)
{
	int err = 0;

	if (ks_node_get(node)) {
		err = sysfs_create_file(&node->kobj, &attr->attr);
		ks_node_put(node);
	}

	return err;
}
EXPORT_SYMBOL(ks_node_create_file);

void ks_node_remove_file(
	struct ks_node *node,
	struct ks_node_attribute *attr)
{
	if (ks_node_get(node)) {
		sysfs_remove_file(&node->kobj, &attr->attr);
		ks_node_put(node);
	}
}
EXPORT_SYMBOL(ks_node_remove_file);

int ks_node_modinit(void)
{
	int err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	ks_nodes_kset.subsys = &kstreamer_subsys;
#else
	ks_nodes_kset.kobj.parent = &kstreamer_subsys.kobj;
#endif
	ks_nodes_kset.ktype = &ks_node_ktype;
	kobject_set_name(&ks_nodes_kset.kobj, "nodes");

	err = kset_register(&ks_nodes_kset);
	if (err < 0)
		goto err_kset_register;

	return 0;

	kset_unregister(&ks_nodes_kset);
err_kset_register:

	return err;
}

void ks_node_modexit(void)
{
	kset_unregister(&ks_nodes_kset);
}

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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <kernel_config.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "channel.h"
#include "duplex.h"
#include "pipeline.h"
#include "netlink.h"
#include "feature.h"

struct kset ks_chans_kset;

static struct list_head ks_chans_list = LIST_HEAD_INIT(ks_chans_list);
static rwlock_t ks_chans_list_lock = RW_LOCK_UNLOCKED;

struct ks_chan *_ks_chan_search_by_id(int id)
{
	struct ks_chan *chan;
	list_for_each_entry(chan, &ks_chans_list, node) {
		if (chan->id == id)
			return chan;
	}

	return NULL;
}

struct ks_chan *ks_chan_get_by_id(int id)
{
	struct ks_chan *chan;

	read_lock(&ks_chans_list_lock);
	chan = ks_chan_get(_ks_chan_search_by_id(id));
	read_unlock(&ks_chans_list_lock);

	return chan;
}

struct ks_chan *ks_chan_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		if(attr->type == KS_CHANATTR_ID)
			return ks_chan_get_by_id(*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

static int _ks_chan_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing chan ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_ks_chan_search_by_id(cur_id))
			return cur_id;
	}
}

int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}

//----------------------------------------------------------------------------

static struct attribute *ks_chan_default_attrs[] =
{
	NULL,
};

#define to_ks_chan_attr(_attr) \
	container_of(_attr, struct ks_chan_attribute, attr)

static ssize_t ks_chan_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct ks_chan_attribute *ks_chan_attr =
					to_ks_chan_attr(attr);
	struct ks_chan *chan = to_ks_chan(kobj);
	ssize_t err;

	if (ks_chan_attr->show)
		err = ks_chan_attr->show(chan, ks_chan_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t ks_chan_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct ks_chan_attribute *ks_chan_attr =
					to_ks_chan_attr(attr);
	struct ks_chan *chan = to_ks_chan(kobj);
	ssize_t err;

	if (ks_chan_attr->store)
		err = ks_chan_attr->store(chan, ks_chan_attr,
					buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops ks_chan_sysfs_ops = {
	.show   = ks_chan_attr_show,
	.store  = ks_chan_attr_store,
};

static void ks_chan_release(struct kobject *kobj)
{
	struct ks_chan *chan = to_ks_chan(kobj);

	ks_debug(3, "ks_chan_release()\n");

	ks_node_put(chan->from);
	ks_node_put(chan->to);

	if (chan->ops->release)
		chan->ops->release(chan);
	else
		kfree(chan);
}

static struct kobj_type ks_chan_ktype = {
	.release	= ks_chan_release,
	.sysfs_ops	= &ks_chan_sysfs_ops,
	.default_attrs	= ks_chan_default_attrs,
};

struct ks_chan *ks_chan_create(
	struct ks_chan *chan,
	struct ks_chan_ops *ops,
	const char *name,
	struct ks_duplex *duplex,
	struct kobject *parent,
	struct ks_node *from,
	struct ks_node *to)
{
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!name);
	BUG_ON(!from);
	BUG_ON(!to);

	if (!chan) {
		chan = kmalloc(sizeof(*chan), GFP_KERNEL);
		if (!chan)
			return NULL;
	}

	memset(chan, 0, sizeof(*chan));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	kobject_init(&chan->kobj);
#else
	kobject_init(&chan->kobj, &ks_chan_ktype);
#endif

	chan->kobj.parent = parent;
	chan->kobj.kset = &ks_chans_kset;
	kobject_set_name(&chan->kobj, "%s", name);

	chan->duplex = duplex;
	chan->ops = ops;
	chan->from = ks_node_get(from);
	chan->to = ks_node_get(to);

	chan->mtu = -1;

	INIT_LIST_HEAD(&chan->pipeline_entry);

	return chan;
}
EXPORT_SYMBOL(ks_chan_create);

int ks_chan_write_to_nlmsg(
	struct ks_chan *chan,
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

	err = ks_netlink_put_attr(skb, KS_CHANATTR_ID,
				&chan->id, sizeof(chan->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_CHAN_DEL) {

		err = ks_netlink_put_attr_path(skb, KS_CHANATTR_PATH,
						&chan->kobj);
		if (err < 0)
			goto err_put_attr;

		err = ks_netlink_put_attr(skb, KS_CHANATTR_FROM,
					&chan->from->id,
					sizeof(chan->from->id));
		if (err < 0)
			goto err_put_attr;

		err = ks_netlink_put_attr(skb, KS_CHANATTR_TO,
					&chan->to->id, sizeof(chan->to->id));
		if (err < 0)
			goto err_put_attr;

		if (chan->ops->get_attr_count &&
		    chan->ops->get_attr) {
			int i;
			int attrs_cnt = 0;

				attrs_cnt = chan->ops->get_attr_count(chan);

			for (i=0; i<attrs_cnt; i++) {

				u8 buf[32];
				u16 type;
				int len = sizeof(buf);

				err = chan->ops->get_attr(chan, i, &type,
								buf, &len);
				if (err < 0)
					goto err_put_attr;

				err = ks_netlink_put_attr(skb, type, buf, len);
				if (err < 0)
					goto err_put_attr;
			}
		}
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

static int ks_chan_update_from_nlmsg(struct ks_chan *chan, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_CHANATTR_ID:
		case KS_CHANATTR_PATH:
		case KS_CHANATTR_FROM:
		case KS_CHANATTR_TO:
			/* Are updates to these allowed? */
		break;

		default:
			if (chan->ops->set_attr) {
				int err;

				err = chan->ops->set_attr(chan, attr->type,
							KS_ATTR_DATA(attr),
							KS_ATTR_PAYLOAD(attr));

				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

static int ks_chan_mcast_send(
	struct ks_chan *chan,
	struct ks_netlink_state *state,
	enum ks_netlink_message_type message_type)
{
	int err;

retry:
	err = ks_netlink_mcast_need_skb(state);
	if (err < 0)
		return err;

	err = ks_chan_write_to_nlmsg(chan, state->mcast_skb, message_type,
						0, state->mcast_seqnum++, 0);
	if (err < 0) {
		ks_netlink_mcast_need_another_skb(state);
		goto retry;
	}

	return 0;
}

static int ks_chan_netlink_respond(
	struct ks_chan *chan,
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	enum ks_netlink_message_type message_type)
{
	int err;

retry:
	ks_netlink_need_skb(state);
	if (!state->out_skb)
		return -ENOMEM;

	err = ks_chan_write_to_nlmsg(chan, state->out_skb,
				message_type,
				req_nlh->nlmsg_pid,
				req_nlh->nlmsg_seq,
				NLM_F_ACK);
	if (err < 0) {
		ks_netlink_flush(state);
		goto retry;
	}

	return 0;
}

int ks_chan_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	int err;
	struct ks_chan *chan;
	int cnt = 1;
  
	ks_netlink_send_ack(state, nlh, NLM_F_MULTI);

	/* No need to read_lock(&ks_chans_list_lock); because we are also
	 * protected by ks_topology_lock semaphore.
	 */

	list_for_each_entry(chan, &ks_chans_list, node) {

retry:
		ks_netlink_need_skb(state);
		if (!state->out_skb)
			return -ENOMEM;

		err = ks_chan_write_to_nlmsg(chan,
					state->out_skb,
					KS_NETLINK_CHAN_NEW,
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

int ks_chan_cmd_set(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	struct ks_chan *chan;
	int err;

	chan = ks_chan_get_by_nlid(nlh);
	if (!chan) {
		printk(KERN_CRIT "Link ID not found\n");
		err = -ENOENT;
		goto err_chan_get;
	}

	err = ks_chan_update_from_nlmsg(chan, nlh);
	if (err < 0)
		goto err_chan_update;

	ks_chan_netlink_respond(chan, state, nlh, KS_NETLINK_CHAN_SET);

	ks_chan_put(chan);

	return 0;

err_chan_update:
	ks_chan_put(chan);
err_chan_get:

	return err;
}

int ks_chan_register_no_topology_lock(struct ks_chan *chan)
{
	int err;

	BUG_ON(!chan);

	write_lock(&ks_chans_list_lock);
	chan->id = _ks_chan_new_id();
	list_add_tail(&ks_chan_get(chan)->node, &ks_chans_list);
	write_unlock(&ks_chans_list_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	err = kobject_add(&chan->kobj);
	if (err < 0)
		goto err_kobject_add;
#else
	err = kobject_add(&chan->kobj, chan->kobj.parent, "%s",
					kobject_name(&chan->kobj));
	if (err < 0)
		goto err_kobject_add;
#endif

	if (chan->duplex) {
		err = sysfs_create_link(
			&chan->kobj,
			&chan->duplex->kobj,
			"duplex");
		if (err < 0)
			goto err_create_chan_duplex;
	}

	BUG_ON(!chan->from);
	err = sysfs_create_link(
		&chan->kobj,
		&chan->from->kobj,
		"from");
	if (err < 0)
		goto err_create_chan_from;

	BUG_ON(!chan->to);
	err = sysfs_create_link(
		&chan->kobj,
		&chan->to->kobj,
		"to");
	if (err < 0)
		goto err_create_chan_to;

	ks_chan_mcast_send(chan, &ks_netlink_state, KS_NETLINK_CHAN_NEW);

	return 0;

	sysfs_remove_link(&chan->kobj, "to");
err_create_chan_to:
	sysfs_remove_link(&chan->kobj, "from");
err_create_chan_from:
	if (chan->duplex)
		sysfs_remove_link(&chan->kobj, "duplex");
err_create_chan_duplex:
	kobject_del(&chan->kobj);
err_kobject_add:
	write_lock(&ks_chans_list_lock);
	list_del(&chan->node);
	write_unlock(&ks_chans_list_lock);
	ks_chan_put(chan);

	return err;
}

int ks_chan_register(struct ks_chan *chan)
{
	int err;

	ks_topology_lock();
	err = ks_chan_register_no_topology_lock(chan);
	ks_topology_unlock();

	return err;
}
EXPORT_SYMBOL(ks_chan_register);

void ks_chan_unregister_no_topology_lock(struct ks_chan *chan)
{
	write_lock_bh(&ks_connection_lock);
	if (chan->pipeline) {
		struct ks_pipeline *pipeline = ks_pipeline_get(chan->pipeline);
		write_unlock_bh(&ks_connection_lock);

		ks_pipeline_unregister_no_topology_lock(pipeline);

		ks_pipeline_put(pipeline);
	} else
		write_unlock_bh(&ks_connection_lock);

	sysfs_remove_link(&chan->kobj, "to");
	sysfs_remove_link(&chan->kobj, "from");

	if (chan->duplex)
		sysfs_remove_link(&chan->kobj, "duplex");

	kobject_del(&chan->kobj);

	write_lock(&ks_chans_list_lock);
	list_del(&chan->node);
	write_unlock(&ks_chans_list_lock);
	ks_chan_put(chan);

	ks_chan_mcast_send(chan, &ks_netlink_state, KS_NETLINK_CHAN_DEL);
}

void ks_chan_unregister(struct ks_chan *chan)
{
	ks_topology_lock();
	ks_chan_unregister_no_topology_lock(chan);
	ks_topology_unlock();
}
EXPORT_SYMBOL(ks_chan_unregister);

void ks_chan_destroy(struct ks_chan *chan)
{
	ks_kobj_waitref(&chan->kobj);
	ks_chan_put(chan);
}
EXPORT_SYMBOL(ks_chan_destroy);

int ks_chan_create_file(
	struct ks_chan *chan,
	struct ks_chan_attribute *attr)
{
	return sysfs_create_file(&chan->kobj, &attr->attr);
}
EXPORT_SYMBOL(ks_chan_create_file);

void ks_chan_remove_file(
	struct ks_chan *chan,
	struct ks_chan_attribute *attr)
{
	sysfs_remove_file(&chan->kobj, &attr->attr);
}
EXPORT_SYMBOL(ks_chan_remove_file);

int ks_chan_modinit(void)
{
	int err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	ks_chans_kset.subsys = &kstreamer_kset;
#else
	ks_chans_kset.kobj.parent = &kstreamer_kset.kobj;
#endif

	err = kobject_set_name(&ks_chans_kset.kobj, "chans");
	if (err < 0)
	        goto err_kobject_set_name;

	err = kset_register(&ks_chans_kset);
	if (err < 0)
		goto err_kset_register;

	return 0;

err_kobject_set_name:
	kset_unregister(&ks_chans_kset);
err_kset_register:

	return err;
}

void ks_chan_modexit(void)
{
	kset_unregister(&ks_chans_kset);
}

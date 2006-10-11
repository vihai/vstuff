/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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
#include "link.h"
#include "duplex.h"
#include "pipeline.h"
#include "netlink.h"
#include "dynattr.h"

struct kset ks_links_kset;

struct list_head ks_links_list = LIST_HEAD_INIT(ks_links_list);
DEFINE_RWLOCK(ks_links_list_lock);

struct ks_link *_ks_link_search_by_id(int id)
{
	struct ks_link *link;
	list_for_each_entry(link, &ks_links_list, node) {
		if (link->id == id)
			return link;
	}

	return NULL;
}

struct ks_link *ks_link_get_by_id(int id)
{
	struct ks_link *link;

	read_lock(&ks_links_list_lock);
	link = ks_link_get(_ks_link_search_by_id(id));
	read_unlock(&ks_links_list_lock);

	return link;
}

struct ks_link *ks_link_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		if(attr->type == KS_LINKATTR_ID)
			return ks_link_get_by_id(*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

static int _ks_link_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing link ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_ks_link_search_by_id(cur_id))
			return cur_id;
	}
}

#if 0
int ks_link_frame_xmit(
	struct ks_link *link,
	struct sk_buff *skb)
{
	if (!link->cxc->ops || !link->cxc->ops->frame_xmit) {
		WARN_ON(1);
		return -ENODEV;
	}

	return link->cxc->ops->frame_xmit(link->cxc, link, skb);
}
EXPORT_SYMBOL(ks_link_frame_xmit);

void ks_link_start_queue(
	struct ks_link *link)
{
	unsigned long flags;

	if (!link->cxc->ops || !link->cxc->ops->start_queue) {
		WARN_ON(1);
		return;
	}

	spin_lock_irqsave(&link->queue_stopped_lock, flags);
	link->cxc->ops->start_queue(link->cxc, link);
	clear_bit(KS_LINK_STATUS_QUEUE_STOPPED, &link->status);
	spin_unlock_irqrestore(&link->queue_stopped_lock, flags);
}
EXPORT_SYMBOL(ks_link_start_queue);

void ks_link_stop_queue(
	struct ks_link *link)
{
	unsigned long flags;

	if (!link->cxc->ops || !link->cxc->ops->stop_queue) {
		WARN_ON(1);
		return;
	}

	spin_lock_irqsave(&link->queue_stopped_lock, flags);
	link->cxc->ops->stop_queue(link->cxc, link);
	set_bit(KS_LINK_STATUS_QUEUE_STOPPED, &link->status);
	spin_unlock_irqrestore(&link->queue_stopped_lock, flags);
}
EXPORT_SYMBOL(ks_link_stop_queue);

void ks_link_wake_queue(
	struct ks_link *link)
{
	unsigned long flags;

	if (!link->cxc->ops || !link->cxc->ops->wake_queue) {
		WARN_ON(1);
		return;
	}

	spin_lock_irqsave(&link->queue_stopped_lock, flags);
	link->cxc->ops->wake_queue(link->cxc, link);
	clear_bit(KS_LINK_STATUS_QUEUE_STOPPED, &link->status);
	spin_unlock_irqrestore(&link->queue_stopped_lock, flags);
}
EXPORT_SYMBOL(ks_link_wake_queue);

void ks_link_rx_error(
	struct ks_link *link,
	enum ks_link_rx_error_code code)
{
	if (!link->cxc->ops || !link->cxc->ops->rx_error) {
		WARN_ON(1);
		return;
	}

	link->cxc->ops->rx_error(link->cxc, link, code);
}
EXPORT_SYMBOL(ks_link_rx_error);

void ks_link_tx_error(
	struct ks_link *link,
	enum ks_link_tx_error_code code)
{
	if (!link->cxc->ops || !link->cxc->ops->tx_error) {
		WARN_ON(1);
		return;
	}

	link->cxc->ops->tx_error(link->cxc, link, code);
}
EXPORT_SYMBOL(ks_link_tx_error);

//----------------------------------------------------------------------------

static ssize_t ks_link_show_framing(
	struct ks_link *link,
	struct ks_link_attribute *attr,
	char *buf)
{
	int len;

	len = snprintf(buf, PAGE_SIZE, "%s\n",
			ks_framing_to_string(link->framing));

	return len;
}

static ssize_t ks_link_store_framing(
	struct ks_link *link,
	struct ks_link_attribute *attr,
	const char *buf,
	size_t count)
{
	int err;
	int len = count;

	while(len > 0) {
		if (buf[len - 1] != '\r' && buf[len - 1] != '\n')
			break;

		len--;
	}

	if (!strncmp(buf, "none", len) &&
			link->framing_avail & KS_LINK_FRAMING_NONE)
		link->framing = KS_LINK_FRAMING_NONE;
	else if (!strncmp(buf, "async", len) &&
			link->framing_avail & KS_LINK_FRAMING_ASYNC)
		link->framing = KS_LINK_FRAMING_ASYNC;
	else if (!strncmp(buf, "hdlc", len) &&
			link->framing_avail & KS_LINK_FRAMING_HDLC)
		link->framing = KS_LINK_FRAMING_HDLC;
	else {
		err = -EINVAL;
		goto err_invalid_framing;
	}

	return count;

err_invalid_framing:

	return err;
}

static KS_LINK_ATTR(framing, S_IRUGO | S_IWUSR,
		ks_link_show_framing,
		ks_link_store_framing);
#endif
//----------------------------------------------------------------------------

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

static struct attribute *ks_link_default_attrs[] =
{
	NULL,
};

#define to_ks_link_attr(_attr) \
	container_of(_attr, struct ks_link_attribute, attr)

static ssize_t ks_link_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct ks_link_attribute *ks_link_attr =
					to_ks_link_attr(attr);
	struct ks_link *link = to_ks_link(kobj);
	ssize_t err;

	if (ks_link_attr->show)
		err = ks_link_attr->show(link, ks_link_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t ks_link_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct ks_link_attribute *ks_link_attr =
					to_ks_link_attr(attr);
	struct ks_link *link = to_ks_link(kobj);
	ssize_t err;

	if (ks_link_attr->store)
		err = ks_link_attr->store(link, ks_link_attr,
					buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops ks_link_sysfs_ops = {
	.show   = ks_link_attr_show,
	.store  = ks_link_attr_store,
};

static void ks_link_release(struct kobject *kobj)
{
	struct ks_link *link = to_ks_link(kobj);

	ks_debug(3, "ks_link_release()\n");

	ks_node_put(link->from);
	ks_node_put(link->to);

	if (link->ops->release)
		link->ops->release(link);
	else {
		ks_msg(KERN_ERR, "vISDN link '%s' does not have a"
			" release() function, it is broken and must be"
			" fixed.\n",
			link->kobj.name);
		WARN_ON(1);
	}
}

void ks_link_del_rcu(struct rcu_head *head)
{
	struct ks_link *link =
		container_of(head, struct ks_link, pipeline_entry_rcu);

	ks_debug(3, "ks_link_del_rcu()\n");

	ks_link_put(link);
}

static struct kobj_type ks_link_ktype = {
	.release	= ks_link_release,
	.sysfs_ops	= &ks_link_sysfs_ops,
	.default_attrs	= ks_link_default_attrs,
};

void ks_link_init(
	struct ks_link *link,
	struct ks_link_ops *ops,
	const char *name,
	struct ks_duplex *duplex,
	struct kobject *parent,
	struct ks_node *from,
	struct ks_node *to)
{
	BUG_ON(!link);
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!name);
	BUG_ON(!from);
	BUG_ON(!to);

	memset(link, 0, sizeof(*link));

	link->kobj.kset = &ks_links_kset;
	link->kobj.parent = parent;

	kobject_init(&link->kobj);
	kobject_set_name(&link->kobj, "%s", name);

	link->duplex = duplex;
	link->ops = ops;
	link->from = ks_node_get(from);
	link->to = ks_node_get(to);

	INIT_LIST_HEAD(&link->pipeline_entry);
	INIT_RCU_HEAD(&link->pipeline_entry_rcu);
}
EXPORT_SYMBOL(ks_link_init);

int ks_link_write_to_nlmsg(
	struct ks_link *link,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	u32 pid, u32 seq, u16 flags)
{
	struct nlmsghdr *nlh;
	unsigned char *oldtail;
	int err = -ENOBUFS;

	oldtail = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, message_type, 0);
	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_LINKATTR_ID,
				&link->id, sizeof(link->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_LINK_DEL) {

		err = ks_netlink_put_attr_path(skb, KS_LINKATTR_PATH,
						&link->kobj);
		if (err < 0)
			goto err_put_attr;

		err = ks_netlink_put_attr(skb, KS_LINKATTR_FROM,
					&link->from->id,
					sizeof(link->from->id));
		if (err < 0)
			goto err_put_attr;

		err = ks_netlink_put_attr(skb, KS_LINKATTR_TO,
					&link->to->id, sizeof(link->to->id));
		if (err < 0)
			goto err_put_attr;

		if (link->ops->get_attr_count &&
		    link->ops->get_attr) {
			int i;
			int attrs_cnt = 0;

				attrs_cnt = link->ops->get_attr_count(link);

			for (i=0; i<attrs_cnt; i++) {
				
				u8 buf[32];
				u16 type;
				int len = sizeof(buf);

				err = link->ops->get_attr(link, i, &type,
								buf, &len);
				if (err < 0)
					goto err_put_attr;

				err = ks_netlink_put_attr(skb, type, buf, len);
				if (err < 0)
					goto err_put_attr;
			}
		}
	}

	nlh->nlmsg_len = skb->tail - oldtail;

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

void ks_link_update_from_nlmsg(struct ks_link *link, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		switch(attr->type) {
		case KS_LINKATTR_ID:
		case KS_LINKATTR_PATH:
		case KS_LINKATTR_FROM:
		case KS_LINKATTR_TO:
			/* Are updates to these allowed? */
		break;

		default:
			if (link->ops->set_attr) {
				int err;

				err = link->ops->set_attr(link, attr->type,
							KS_ATTR_DATA(attr),
							KS_ATTR_PAYLOAD(attr));

				if (err < 0) {
					// FIXME
				}
			}
		}
	}
}

static int ks_link_netlink_notification(
	struct ks_link *link,
	enum ks_netlink_message_type message_type,
	u32 pid)
{
	struct sk_buff *skb;
	int err = -EINVAL;

	skb = alloc_skb(NLMSG_SPACE(280), GFP_KERNEL);
	if (!skb) {
	        netlink_set_err(ksnl, 0, KS_NETLINK_GROUP_TOPOLOGY, ENOBUFS);
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	NETLINK_CB(skb).pid = 0;
	NETLINK_CB(skb).dst_pid = pid;
	NETLINK_CB(skb).dst_group = pid ? KS_NETLINK_GROUP_TOPOLOGY : 0;

	err = ks_link_write_to_nlmsg(link, skb, message_type, pid, 0, 0);
	if (err < 0)
		goto err_fill_msg;

	netlink_broadcast(ksnl, skb, 0,
		KS_NETLINK_GROUP_TOPOLOGY,
		GFP_KERNEL);

	return 0;

err_fill_msg:
	kfree_skb(skb);
err_alloc_skb:

	return err;
}

void ks_link_netlink_dump(struct ks_netlink_xact *xact)
{
	struct ks_link *link;
	int err;
       
	list_for_each_entry(link, &ks_links_kset.list, kobj.entry) {
		ks_netlink_xact_need_skb(xact);
		if (!xact->dump_skb)
			return;

		err = ks_link_write_to_nlmsg(link, xact->dump_skb,
					KS_NETLINK_LINK_NEW,
					xact->pid,
					0,
					NLM_F_MULTI);
		if (err < 0)
			ks_netlink_xact_flush(xact);
	}

	ks_netlink_xact_send_control(xact, NLMSG_DONE, NLM_F_MULTI);
}

int ks_link_register(struct ks_link *link)
{
	int err;

	BUG_ON(!link);

	write_lock(&ks_links_list_lock);
	link->id = _ks_link_new_id();
	list_add_tail(&ks_link_get(link)->node,
		&ks_links_list);
	write_unlock(&ks_links_list_lock);

	err = kobject_add(&link->kobj);
	if (err < 0)
		goto err_kobject_add;

	if (link->duplex) {
		err = sysfs_create_link(
			&link->kobj,
			&link->duplex->kobj,
			"duplex");
		if (err < 0)
			goto err_create_link_duplex;
	}

	BUG_ON(!link->from);
	err = sysfs_create_link(
		&link->kobj,
		&link->from->kobj,
		"from");
	if (err < 0)
		goto err_create_link_from;

	BUG_ON(!link->to);
	err = sysfs_create_link(
		&link->kobj,
		&link->to->kobj,
		"to");
	if (err < 0)
		goto err_create_link_to;

	ks_link_netlink_notification(link, KS_NETLINK_LINK_NEW, 0);

	return 0;

	sysfs_remove_link(&link->kobj, "to");
err_create_link_to:
	sysfs_remove_link(&link->kobj, "from");
err_create_link_from:
	if (link->duplex)
		sysfs_remove_link(&link->kobj, "duplex");
err_create_link_duplex:
	kobject_del(&link->kobj);
err_kobject_add:
	write_lock(&ks_links_list_lock);
	list_del(&link->node);
	ks_link_put(link);
	write_unlock(&ks_links_list_lock);

	return err;
}
EXPORT_SYMBOL(ks_link_register);

void ks_link_unregister(struct ks_link *link)
{
	ks_link_netlink_notification(link, KS_NETLINK_LINK_DEL, 0);

	if (link->pipeline) {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get(link->pipeline);

		ks_pipeline_setstatus(pipeline, KS_PIPELINE_STATUS_NULL);
		ks_pipeline_unregister(pipeline);

		ks_pipeline_put(pipeline);
		pipeline = NULL;
	}

	sysfs_remove_link(&link->kobj, "to");
	sysfs_remove_link(&link->kobj, "from");

	if (link->duplex)
		sysfs_remove_link(&link->kobj, "duplex");

	kobject_del(&link->kobj);

	write_lock(&ks_links_list_lock);
	list_del(&link->node);
	ks_link_put(link);
	write_unlock(&ks_links_list_lock);
}
EXPORT_SYMBOL(ks_link_unregister);

int ks_link_create_file(
	struct ks_link *link,
	struct ks_link_attribute *attr)
{
	return sysfs_create_file(&link->kobj, &attr->attr);
}
EXPORT_SYMBOL(ks_link_create_file);

void ks_link_remove_file(
	struct ks_link *link,
	struct ks_link_attribute *attr)
{
	sysfs_remove_file(&link->kobj, &attr->attr);
}
EXPORT_SYMBOL(ks_link_remove_file);

int ks_link_modinit(void)
{
	int err;

	ks_links_kset.subsys = &kstreamer_subsys;
	ks_links_kset.ktype = &ks_link_ktype;
	kobject_set_name(&ks_links_kset.kobj, "links");

	err = kset_register(&ks_links_kset);
	if (err < 0)
		goto err_kset_register;

	return 0;

	kset_unregister(&ks_links_kset);
err_kset_register:

	return err;
}

void ks_link_modexit(void)
{
	kset_unregister(&ks_links_kset);
}

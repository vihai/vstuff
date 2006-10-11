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
#include <linux/spinlock.h>
#include <linux/list.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "link.h"
#include "pipeline.h"
#include "router.h"

struct kset ks_pipelines_kset;

struct list_head ks_pipelines_list = LIST_HEAD_INIT(ks_pipelines_list);
DECLARE_RWSEM(ks_pipelines_list_sem);

struct ks_pipeline *_ks_pipeline_search_by_id(int id)
{
	struct ks_pipeline *pipeline;
	list_for_each_entry(pipeline, &ks_pipelines_list, node) {
		if (pipeline->id == id)
			return pipeline;
	}

	return NULL;
}

struct ks_pipeline *ks_pipeline_get_by_id(int id)
{
	struct ks_pipeline *pipeline;

	down_read(&ks_pipelines_list_sem);
	pipeline = ks_pipeline_get(_ks_pipeline_search_by_id(id));
	up_read(&ks_pipelines_list_sem);

	return pipeline;
}

static int _ks_pipeline_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing pipeline ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_ks_pipeline_search_by_id(cur_id))
			return cur_id;
	}
}

static const char *ks_pipeline_status_to_text(
	enum ks_pipeline_status status)
{
	switch(status) {
	case KS_PIPELINE_STATUS_NULL:
		return "NULL";
	case KS_PIPELINE_STATUS_CONNECTED:
		return "CONNECTED";
	case KS_PIPELINE_STATUS_OPEN:
	     	return "OPEN";
	case KS_PIPELINE_STATUS_FLOWING:
		return "FLOWING";
	}

	return "*INVALID*";
}

static void ks_pipeline_set_status(
	struct ks_pipeline *pipeline,
	enum ks_pipeline_status status)
{
	ks_debug(2, "Pipeline %d changed status from %s to %s\n",
			pipeline->id,
			ks_pipeline_status_to_text(pipeline->status),
			ks_pipeline_status_to_text(status));

	pipeline->status = status;
}

int ks_pipeline_write_to_nlmsg(
	struct ks_pipeline *pipeline,
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

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_ID, &pipeline->id,
						sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_PIPELINE_DEL) {
		err = ks_netlink_put_attr_path(skb, KS_PIPELINEATTR_PATH,
						&pipeline->kobj);
		if (err < 0)
			goto err_put_attr;
	}

	nlh->nlmsg_len = skb->tail - oldtail;

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

struct ks_pipeline *ks_pipeline_create_from_nlmsg(
	struct nlmsghdr *nlh, int *err)
{
	struct ks_pipeline *pipeline;
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);
	enum ks_pipeline_status status = KS_PIPELINE_STATUS_NULL;

	pipeline = ks_pipeline_alloc();
	if (!pipeline) {
		*err = -ENOMEM;
		goto err_pipeline_alloc;
	}

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {
		switch(attr->type) {
		case KS_PIPELINEATTR_STATUS:
			status = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_LINK_ID: {
			struct ks_link *link;

			if (pipeline->status != KS_PIPELINE_STATUS_NULL) {
				*err = -EINVAL;
				goto err_invalid_status;
			}

			link = ks_link_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
			if (!link) {
				printk(KERN_CRIT "Link ID not found\n");
				// FIXME
				*err = -ENODEV;
				goto err_link_not_found;
			}

			list_add_tail(
				&ks_link_get(link)->pipeline_entry,
				&pipeline->entries);

			link->pipeline = ks_pipeline_get(pipeline);

			ks_link_put(link);
		}
		break;

		default:
			printk(KERN_CRIT "  Unexpected attribute %d\n",
					attr->type);

			*err = -EINVAL;
			goto err_unexpected_attribute;
		break;
		}
	}

	*err = ks_pipeline_setstatus(pipeline, status);
	if (*err < 0)
		goto err_invalid_status;

	return pipeline;

err_unexpected_attribute:
err_link_not_found:
err_invalid_status:
	ks_pipeline_put(pipeline);
	pipeline = NULL;
err_pipeline_alloc:

	return NULL;
}

int ks_pipeline_update_from_nlsmsg(
	struct ks_pipeline *pipeline,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);
	enum ks_pipeline_status status = KS_PIPELINE_STATUS_NULL;
	int err;

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {
		switch(attr->type) {
		case KS_PIPELINEATTR_STATUS:
			status = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_LINK_ID: {
			struct ks_link *link;

			if (pipeline->status != KS_PIPELINE_STATUS_NULL) {
				err = -EINVAL;
				goto err_invalid_status;
			}

			link = ks_link_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
			if (!link) {
				printk(KERN_CRIT "Link ID not found\n");
				err = -ENODEV;
				// FIXME
				goto err_link_not_found;
			}

			list_add_tail(
				&ks_link_get(link)->pipeline_entry,
				&pipeline->entries);

			link->pipeline = ks_pipeline_get(pipeline);

			ks_link_put(link);
		}
		break;

		default:
			printk(KERN_CRIT "  Unexpected attribute %d\n",
					attr->type);

			err = -EINVAL;
			goto err_unexpected_attribute;
		break;
		}
	}

	err = ks_pipeline_setstatus(pipeline, status);
	if (err < 0)
		goto err_invalid_status;

	return 0;

err_unexpected_attribute:
err_link_not_found:
err_invalid_status:

	return err;
}

static int ks_pipeline_netlink_notification(
	struct ks_pipeline *pipeline,
	enum ks_netlink_message_type message_type,
	u32 pid)
{
	struct sk_buff *skb;
	int err = -EINVAL;

	skb = alloc_skb(NLMSG_SPACE(257), GFP_KERNEL);
	if (!skb) {
	        netlink_set_err(ksnl, 0, KS_NETLINK_GROUP_TOPOLOGY, ENOBUFS);
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	NETLINK_CB(skb).pid = 0;
	NETLINK_CB(skb).dst_pid = pid;
	NETLINK_CB(skb).dst_group = pid ? KS_NETLINK_GROUP_TOPOLOGY : 0;

	err = ks_pipeline_write_to_nlmsg(pipeline, skb, message_type,
						pid, 0, 0);
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

void ks_pipeline_netlink_dump(struct ks_netlink_xact *xact)
{
	struct ks_pipeline *pipeline;
	int err;
       
	list_for_each_entry(pipeline, &ks_pipelines_kset.list, kobj.entry) {
		ks_netlink_xact_need_skb(xact);
		if (!xact->dump_skb)
			return;

		err = ks_pipeline_write_to_nlmsg(pipeline, xact->dump_skb,
					KS_NETLINK_PIPELINE_NEW,
					xact->pid,
					0,
					NLM_F_MULTI);
		if (err < 0)
			ks_netlink_xact_flush(xact);
	}

	ks_netlink_xact_send_control(xact, NLMSG_DONE, NLM_F_MULTI);
}

void ks_pipeline_init(struct ks_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));

	kobject_init(&pipeline->kobj);
	pipeline->kobj.kset = kset_get(&ks_pipelines_kset);

	INIT_LIST_HEAD(&pipeline->entries);

	init_rwsem(&pipeline->lock);

	pipeline->status = KS_PIPELINE_STATUS_NULL;
}

struct ks_pipeline *ks_pipeline_alloc()
{
	struct ks_pipeline *pipeline;

	pipeline = kmalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return NULL;

	ks_pipeline_init(pipeline);

	return pipeline;
}

int ks_pipeline_register(struct ks_pipeline *pipeline)
{
	int err;

	BUG_ON(!pipeline);

	down_write(&ks_pipelines_list_sem);
	pipeline->id = _ks_pipeline_new_id();
	list_add_tail(&ks_pipeline_get(pipeline)->node,
		&ks_pipelines_list);
	up_write(&ks_pipelines_list_sem);

	kobject_set_name(&pipeline->kobj, "%06d", pipeline->id);

	err = kobject_add(&pipeline->kobj);
	if (err < 0)
		goto err_kobject_add;

/*	if (pipeline->duplex) {
		err = sysfs_create_link(
			&pipeline->kobj,
			&pipeline->duplex->kobj,
			"duplex");
		if (err < 0)
			goto err_create_link_duplex;
	}

	BUG_ON(!pipeline->from);
	err = sysfs_create_link(
		&pipeline->kobj,
		&pipeline->from->kobj,
		"from");
	if (err < 0)
		goto err_create_link_from;

	BUG_ON(!pipeline->to);
	err = sysfs_create_link(
		&pipeline->kobj,
		&pipeline->to->kobj,
		"to");
	if (err < 0)
		goto err_create_link_to;*/

	ks_pipeline_netlink_notification(pipeline, KS_NETLINK_PIPELINE_NEW, 0);

	return 0;

/*	sysfs_remove_pipeline(&pipeline->kobj, "to");
err_create_pipeline_to:
	sysfs_remove_pipeline(&pipeline->kobj, "from");
err_create_pipeline_from:
	if (pipeline->duplex)
		sysfs_remove_pipeline(&pipeline->kobj, "duplex");
err_create_pipeline_link:*/
	kobject_del(&pipeline->kobj);
err_kobject_add:
	down_write(&ks_pipelines_list_sem);
	list_del(&pipeline->node);
	ks_pipeline_put(pipeline);
	up_write(&ks_pipelines_list_sem);

	return err;
}
EXPORT_SYMBOL(ks_pipeline_register);

void ks_pipeline_unregister(struct ks_pipeline *pipeline)
{
	ks_pipeline_netlink_notification(pipeline, KS_NETLINK_PIPELINE_DEL, 0);

/*	sysfs_remove_pipeline(&pipeline->kobj, "to");
	sysfs_remove_pipeline(&pipeline->kobj, "from");

	if (pipeline->duplex)
		sysfs_remove_pipeline(&pipeline->kobj, "duplex");*/

	ks_pipeline_setstatus(pipeline, KS_PIPELINE_STATUS_NULL);

	kobject_del(&pipeline->kobj);

	down_write(&ks_pipelines_list_sem);
	list_del(&pipeline->node);
	ks_pipeline_put(pipeline);
	up_write(&ks_pipelines_list_sem);
}
EXPORT_SYMBOL(ks_pipeline_unregister);

void ks_pipeline_dump(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;

	printk(KERN_CRIT " ");

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		printk("%s/%s/%s =====(%s/%s/%s)=====> ",
			link->from->kobj.parent->parent->name,
			link->from->kobj.parent->name,
			link->from->kobj.name,
			link->kobj.parent->parent->name,
			link->kobj.parent->name,
			link->kobj.name);

		prev_link = link;
	}

	if (prev_link)
		printk("%s/%s\n",
			prev_link->to->kobj.parent->name,
			prev_link->to->kobj.name);
}

#if 0
static int ks_pipeline_negotiate_mtu(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	int min_mtu = 65536;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {
		if (link->framed_mtu != -1 &&
		    link->framed_mtu < min_mtu)
			min_mtu = link->framed_mtu;
	}

	return min_mtu;
}

static int ks_pipeline_negotiate_framing(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	int framing = 0x7FFFFFFF;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {
		framing &= link->framing_avail;
	}

	return framing ? (1 << (ffs(framing) - 1)) : 0;
}
#endif

/*-------------------------- NULL <=> CONNECTED -----------------------------*/

static void ks_pipeline_connected_to_null(
	struct ks_pipeline *pipeline,
	struct ks_link *stop_at)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link == stop_at)
			goto done;

		if (link->ops->disconnect)
			link->ops->disconnect(link);

		if (link->from->ops->disconnect)
			link->from->ops->disconnect(
				link->from, link, prev_link);

		prev_link = link;
	}

	if (prev_link && prev_link->to->ops->disconnect)
		prev_link->to->ops->disconnect(
			prev_link->to, NULL, link);

done:

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		struct ks_pipeline *pipeline = link->pipeline;

		link->pipeline = NULL;
		wmb();

		list_del_rcu(&link->pipeline_entry);
		call_rcu(&link->pipeline_entry_rcu, ks_link_del_rcu);

		ks_pipeline_put(pipeline);
	}

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_NULL);
}

static int ks_pipeline_null_to_connected(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;
	int err;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link->from->ops->connect) {
			err = link->from->ops->connect(
				link->from, link, prev_link);
			if (err < 0)
				goto failed;
		}

		if (link->ops->connect) {
			err = link->ops->connect(link);
			if (err < 0) {
				if (link->from->ops->disconnect)
					link->from->ops->disconnect(link->from,
							link, prev_link);

				goto failed;
			}
		}

		prev_link = link;
	}

	if (prev_link &&
	    prev_link->to->ops->connect)
		prev_link->to->ops->connect(
			prev_link->to, NULL, link);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_CONNECTED);

	return 0;

failed:

	ks_pipeline_connected_to_null(pipeline, prev_link);

	return err;
}

/*------------------------- CONNECTED <=> OPEN -----------------------------*/

static void ks_pipeline_open_to_connected(
	struct ks_pipeline *pipeline,
	struct ks_link *stop_at)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link == stop_at)
			goto done;

		if (link->ops->close)
			link->ops->close(link);

		if (link->from->ops->close)
			link->from->ops->close(
				link->from, link, prev_link);

		prev_link = link;
	}

	if (prev_link && prev_link->to->ops->close)
		prev_link->to->ops->close(
			prev_link->to, NULL, link);

done:

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_CONNECTED);
}

static int ks_pipeline_connected_to_open(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;
	int err;

//	pipeline->mtu = ks_pipeline_negotiate_mtu(pipeline);
//	pipeline->framing = ks_pipeline_negotiate_framing(pipeline);

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link->from->ops->open) {
			err = link->from->ops->open(
				link->from, link, prev_link);
			if (err < 0)
				goto failed;
		}

		if (link->ops->open) {
			err = link->ops->open(link);
			if (err < 0) {
				if (link->from->ops->close)
					link->from->ops->close(link->from,
							link, prev_link);

				goto failed;
			}
		}

		prev_link = link;
	}

	if (prev_link && prev_link->to->ops->open)
		prev_link->to->ops->open(
			prev_link->to, NULL, link);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_OPEN);

	return 0;

failed:

	ks_pipeline_open_to_connected(pipeline, prev_link);

	return err;
}

/* -------------------------- OPEN <=> FLOWING -----------------------------*/

static void ks_pipeline_flowing_to_open(
	struct ks_pipeline *pipeline,
	struct ks_link *stop_at)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link == stop_at)
			goto done;

		if (link->ops->stop)
			link->ops->stop(link);

		if (link->from->ops->stop)
			link->from->ops->stop(
				link->from, link, prev_link);

		prev_link = link;
	}

	if (prev_link && prev_link->to->ops->stop)
		prev_link->to->ops->stop(
			prev_link->to, NULL, link);

done:

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_OPEN);
}

static int ks_pipeline_open_to_flowing(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;
	int err;

	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link->from->ops->start) {
			err = link->from->ops->start(
				link->from, link, prev_link);
			if (err < 0)
				goto failed;
		}

		if (link->ops->start) {
			err = link->ops->start(link);
			if (err < 0) {
				if (link->from->ops->stop)
					link->from->ops->stop(link->from,
							link, prev_link);

				goto failed;
			}
		}

		prev_link = link;
	}

	if (prev_link && prev_link->to->ops->start)
		prev_link->to->ops->start(
			prev_link->to, NULL, link);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_FLOWING);

	return 0;

failed:

	ks_pipeline_flowing_to_open(pipeline, prev_link);

	return err;
}

int ks_pipeline_setstatus(
	struct ks_pipeline *pipeline,
	enum ks_pipeline_status status)
{
	int err;

	switch(pipeline->status) {
	case KS_PIPELINE_STATUS_NULL:
		switch(status) {
		case KS_PIPELINE_STATUS_NULL:
		break;

		case KS_PIPELINE_STATUS_CONNECTED:
			err = ks_pipeline_null_to_connected(pipeline);
			if (err < 0)
				goto failed;
		break;

		case KS_PIPELINE_STATUS_OPEN:
			err = ks_pipeline_null_to_connected(pipeline);
			if (err < 0)
				goto failed;

			err = ks_pipeline_connected_to_open(pipeline);
			if (err < 0)
				goto failed;
		break;

		case KS_PIPELINE_STATUS_FLOWING:
			err = ks_pipeline_null_to_connected(pipeline);
			if (err < 0)
				goto failed;

			err = ks_pipeline_connected_to_open(pipeline);
			if (err < 0)
				goto failed;

			err = ks_pipeline_open_to_flowing(pipeline);
			if (err < 0)
				goto failed;
		break;
		}
	break;

	case KS_PIPELINE_STATUS_CONNECTED:
		switch(status) {
		case KS_PIPELINE_STATUS_NULL:
			ks_pipeline_connected_to_null(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_CONNECTED:
		break;

		case KS_PIPELINE_STATUS_OPEN:
			err = ks_pipeline_connected_to_open(pipeline);
			if (err < 0)
				goto failed;
		break;

		case KS_PIPELINE_STATUS_FLOWING:
			err = ks_pipeline_connected_to_open(pipeline);
			if (err < 0)
				goto failed;

			err = ks_pipeline_open_to_flowing(pipeline);
			if (err < 0)
				goto failed;
		break;
		}
	break;

	case KS_PIPELINE_STATUS_OPEN:
		switch(status) {
		case KS_PIPELINE_STATUS_NULL:
			ks_pipeline_open_to_connected(pipeline, NULL);
			ks_pipeline_connected_to_null(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_CONNECTED:
			ks_pipeline_open_to_connected(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_OPEN:
		break;

		case KS_PIPELINE_STATUS_FLOWING:
			err = ks_pipeline_open_to_flowing(pipeline);
			if (err < 0)
				goto failed;
		break;
		}
	break;

	case KS_PIPELINE_STATUS_FLOWING:
		switch(status) {
		case KS_PIPELINE_STATUS_NULL:
			ks_pipeline_flowing_to_open(pipeline, NULL);
			ks_pipeline_open_to_connected(pipeline, NULL);
			ks_pipeline_connected_to_null(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_CONNECTED:
			ks_pipeline_flowing_to_open(pipeline, NULL);
			ks_pipeline_open_to_connected(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_OPEN:
			ks_pipeline_flowing_to_open(pipeline, NULL);
		break;

		case KS_PIPELINE_STATUS_FLOWING:
		break;
		}
	break;
	}

	return 0;

failed:

	return err;
}
EXPORT_SYMBOL(ks_pipeline_setstatus);

#if 0
int ks_pipeline_connect(struct ks_pipeline *pipeline)
{
	int err = 0;

	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_NULL) {
		err = ks_pipeline_null_to_connected(pipeline);
		if (err < 0)
			goto failed;
	}

failed:
	up_write(&pipeline->lock);

	return err;
}

void ks_pipeline_disconnect(struct ks_pipeline *pipeline)
{
	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_FLOWING)
		ks_pipeline_flowing_to_open(pipeline, NULL);

	if (pipeline->status == KS_PIPELINE_STATUS_OPEN)
		ks_pipeline_open_to_connected(pipeline, NULL);

	if (pipeline->status == KS_PIPELINE_STATUS_CONNECTED)
		ks_pipeline_connected_to_null(pipeline, NULL);

	up_write(&pipeline->lock);
}
EXPORT_SYMBOL(ks_pipeline_disconnect);

int ks_pipeline_open(struct ks_pipeline *pipeline)
{
	int err = 0;

	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_NULL) {
		err = ks_pipeline_null_to_connected(pipeline);
		if (err < 0)
			goto failed;
	}

	if (pipeline->status == KS_PIPELINE_STATUS_CONNECTED) {
		err = ks_pipeline_connected_to_open(pipeline);
		if (err < 0)
			goto failed;
	}

failed:

	up_write(&pipeline->lock);

	return err;
}
EXPORT_SYMBOL(ks_pipeline_open);

void ks_pipeline_close(struct ks_pipeline *pipeline)
{
	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_FLOWING)
		ks_pipeline_flowing_to_open(pipeline, NULL);

	if (pipeline->status == KS_PIPELINE_STATUS_OPEN)
		ks_pipeline_open_to_connected(pipeline, NULL);

	up_write(&pipeline->lock);
}
EXPORT_SYMBOL(ks_pipeline_close);

int ks_pipeline_start(struct ks_pipeline *pipeline)
{
	int err = 0;

	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_NULL) {
		err = ks_pipeline_null_to_connected(pipeline);
		if (err < 0)
			goto failed;
	}

	if (pipeline->status == KS_PIPELINE_STATUS_CONNECTED) {
		err = ks_pipeline_connected_to_open(pipeline);
		if (err < 0)
			goto failed;
	}

	if (pipeline->status == KS_PIPELINE_STATUS_OPEN) {
		err = ks_pipeline_open_to_flowing(pipeline);
		if (err < 0)
			goto failed;
	}

failed:
	up_write(&pipeline->lock);

	return err;
}
EXPORT_SYMBOL(ks_pipeline_start);

void ks_pipeline_stop(struct ks_pipeline *pipeline)
{
	down_write(&pipeline->lock);

	if (pipeline->status == KS_PIPELINE_STATUS_FLOWING)
		ks_pipeline_flowing_to_open(pipeline, NULL);

	up_write(&pipeline->lock);
}
EXPORT_SYMBOL(ks_pipeline_stop);
#endif

void ks_pipeline_stimulate(struct ks_pipeline *pipeline)
{
	struct ks_link *link;
	struct ks_link *prev_link = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(link, &pipeline->entries, pipeline_entry) {

		if (link->ops->stimulus)
			link->ops->stimulus(link);

		if (link->from->ops->stimulus)
			link->from->ops->stimulus(
				link->from, link, prev_link);
	}

	if (prev_link && prev_link->to->ops->stimulus)
		prev_link->to->ops->stimulus(
			prev_link->to, NULL, link);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ks_pipeline_stimulate);











struct ks_link *ks_pipeline_prev(struct ks_link *link)
{
	// FIXME LOCKING!!!

	if (!link->pipeline)
		return NULL;

	if (link->pipeline_entry.prev == &link->pipeline->entries)
		return NULL;

	return list_entry(link->pipeline_entry.prev, struct ks_link,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_prev);

struct ks_link *ks_pipeline_next(struct ks_link *link)
{

	if (!link->pipeline)
		return NULL;

	if (link->pipeline_entry.next == &link->pipeline->entries)
		return NULL;

	return list_entry(link->pipeline_entry.next, struct ks_link,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_next);

struct ks_link *ks_pipeline_first_link(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.next == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.next, struct ks_link,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_first_link);

struct ks_link *ks_pipeline_last_link(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.prev == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.prev, struct ks_link,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_last_link);

struct ks_node *ks_pipeline_first_node(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.next == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.next, struct ks_link,
						pipeline_entry)->from;
}
EXPORT_SYMBOL(ks_pipeline_first_node);

struct ks_node *ks_pipeline_last_node(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.prev == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.prev, struct ks_link,
						pipeline_entry)->to;
}
EXPORT_SYMBOL(ks_pipeline_last_node);

/*---------------------------------------------------------------------------*/

static ssize_t ks_pipeline_show_framing(
	struct ks_pipeline *pipeline,
	struct ks_pipeline_attribute *attr,
	char *buf)
{
	int len;

	// FIXME LOCKING
	len = snprintf(buf, PAGE_SIZE, "%d\n", pipeline->framing);

	return len;
}

static KS_PIPELINE_ATTR(framing, S_IRUGO,
		ks_pipeline_show_framing,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t ks_pipeline_show_mtu(
	struct ks_pipeline *pipeline,
	struct ks_pipeline_attribute *attr,
	char *buf)
{
	int len;

	// FIXME LOCKING
	len = snprintf(buf, PAGE_SIZE, "%d\n", pipeline->mtu);

	return len;
}

static KS_PIPELINE_ATTR(mtu, S_IRUGO,
		ks_pipeline_show_mtu,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t ks_pipeline_show_status(
	struct ks_pipeline *pipeline,
	struct ks_pipeline_attribute *attr,
	char *buf)
{
	int len;

	// FIXME LOCKING
	len = snprintf(buf, PAGE_SIZE, "%d\n", pipeline->status);

	return len;
}

static KS_PIPELINE_ATTR(status, S_IRUGO,
		ks_pipeline_show_status,
		NULL);

/*---------------------------------------------------------------------------*/

static struct attribute *ks_pipeline_default_attrs[] =
{
	&ks_pipeline_attr_framing.attr,
	&ks_pipeline_attr_mtu.attr,
	&ks_pipeline_attr_status.attr,
	NULL,
};

#define to_ks_pipeline_attr(_attr) \
	container_of(_attr, struct ks_pipeline_attribute, attr)

static ssize_t ks_pipeline_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct ks_pipeline_attribute *ks_pipeline_attr =
					to_ks_pipeline_attr(attr);
	struct ks_pipeline *ks_pipeline = to_ks_pipeline(kobj);
	ssize_t err;

	if (ks_pipeline_attr->show)
		err = ks_pipeline_attr->show(ks_pipeline,
					ks_pipeline_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t ks_pipeline_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct ks_pipeline_attribute *ks_pipeline_attr =
					to_ks_pipeline_attr(attr);
	struct ks_pipeline *ks_pipeline = to_ks_pipeline(kobj);
	ssize_t err;

	if (ks_pipeline_attr->store)
		err = ks_pipeline_attr->store(
				ks_pipeline, ks_pipeline_attr,
				buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops ks_pipeline_sysfs_ops = {
	.show   = ks_pipeline_attr_show,
	.store  = ks_pipeline_attr_store,
};

static void ks_pipeline_release(struct kobject *kobj)
{
	struct ks_pipeline *pipeline = to_ks_pipeline(kobj);

	printk(KERN_CRIT "pipeline release\n");

	kfree(pipeline);
}

static struct kobj_type ks_pipeline_ktype = {
	.release	= ks_pipeline_release,
	.sysfs_ops	= &ks_pipeline_sysfs_ops,
	.default_attrs	= ks_pipeline_default_attrs,
};

int ks_pipeline_modinit()
{
	int err;

	ks_pipelines_kset.subsys = &kstreamer_subsys;
	ks_pipelines_kset.ktype = &ks_pipeline_ktype;
	kobject_set_name(&ks_pipelines_kset.kobj, "pipelines");

	err = kset_register(&ks_pipelines_kset);
	if (err < 0)
		goto err_kset_register;

	return 0;

	kset_unregister(&ks_pipelines_kset);
err_kset_register:

	return err;
}

void ks_pipeline_modexit()
{
	kset_unregister(&ks_pipelines_kset);
}

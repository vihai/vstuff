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
#include <linux/spinlock.h>
#include <linux/list.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"

rwlock_t ks_connection_lock = RW_LOCK_UNLOCKED;

struct kset ks_pipelines_kset;

struct list_head ks_pipelines_list =
	LIST_HEAD_INIT(ks_pipelines_list);
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


struct ks_pipeline *ks_pipeline_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_PIPELINEATTR_ID) {
			return ks_pipeline_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
		}
	}

	return NULL;
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

#if 0
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
#endif

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	oldtail = skb->tail;
#else
	oldtail = skb_tail_pointer(skb);
#endif

	nlh = NLMSG_PUT(skb, pid, seq, message_type, 0);
	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_ID, &pipeline->id,
						sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_PIPELINE_DEL) {
		err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_STATUS,
						&pipeline->status,
						sizeof(pipeline->status));
		if (err < 0)
			goto err_put_attr;

		err = ks_netlink_put_attr_path(skb, KS_PIPELINEATTR_PATH,
						&pipeline->kobj);
		if (err < 0)
			goto err_put_attr;


		{
		struct ks_chan *chan;
		list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {
			err = ks_netlink_put_attr(skb,
						KS_PIPELINEATTR_CHAN_ID,
						&chan->id,
						sizeof(chan->id));
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

static int ks_pipeline_broadcast_netlink_notification(
	struct ks_pipeline *pipeline,
	enum ks_netlink_message_type message_type)
{
	struct sk_buff *skb;
	int err = -EINVAL;

	skb = alloc_skb(NLMSG_SPACE(257), GFP_KERNEL);
	if (!skb) {
	        //netlink_set_err(ksnl, 0, KS_NETLINK_GROUP_TOPOLOGY, ENOBUFS);
	        // FIXME, set_err is really needed, but kernel people removed
	        // the EXPORT_SYMBOL
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	NETLINK_CB(skb).pid = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	NETLINK_CB(skb).dst_pid = 0;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	NETLINK_CB(skb).dst_groups = (1 << KS_NETLINK_GROUP_TOPOLOGY);
#else
	NETLINK_CB(skb).dst_group = KS_NETLINK_GROUP_TOPOLOGY;
#endif

	err = ks_pipeline_write_to_nlmsg(pipeline, skb, message_type, 0, 0, 0);
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

static int ks_pipeline_xact_write(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact,
	enum ks_netlink_message_type message_type)
{
	int err = -EINVAL;

retry:
	ks_xact_need_skb(xact);
	if (!xact->out_skb)
		return -ENOMEM;

	err = ks_pipeline_write_to_nlmsg(pipeline, xact->out_skb, message_type,
						xact->pid, xact->id, 0);
	if (err < 0) {
		ks_xact_flush(xact);
		goto retry;
	}

	ks_xact_flush(xact);

	return 0;
}

struct ks_pipeline *ks_pipeline_create_from_nlmsg(
	struct nlmsghdr *nlh, int *errp)
{
	struct ks_pipeline *pipeline;
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);
	enum ks_pipeline_status status = KS_PIPELINE_STATUS_NULL;
	int err;

	pipeline = ks_pipeline_create(NULL);
	if (!pipeline) {
		err = -ENOMEM;
		goto err_pipeline_alloc;
	}

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {
		switch(attr->type) {
		case KS_PIPELINEATTR_STATUS:
			status = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_CHAN_ID: {
			struct ks_chan *chan;

			if (pipeline->status != KS_PIPELINE_STATUS_NULL) {
				err = -EINVAL;
				goto err_invalid_status;
			}

			chan = ks_chan_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
			if (!chan) {
				err = -ENODEV;
				goto err_chan_not_found;
			}

			if (chan->pipeline) {
				err = -EBUSY;
				ks_chan_put(chan);
				goto err_chan_is_busy;
			}

			write_lock_bh(&ks_connection_lock);
			chan->pipeline = ks_pipeline_get(pipeline);

			list_add_tail(
				&ks_chan_get(chan)->pipeline_entry,
				&pipeline->entries);
			write_unlock_bh(&ks_connection_lock);

			ks_chan_put(chan);
		}
		break;

		default:
			ks_msg(KERN_WARNING, "Unexpected attribute %d\n",
					attr->type);

			err = -EINVAL;
			goto err_unexpected_attribute;
		break;
		}
	}

	if (status == KS_PIPELINE_STATUS_NULL)
		status = KS_PIPELINE_STATUS_CONNECTED;

	err = ks_pipeline_change_status(pipeline, status);
	if (err < 0)
		goto err_invalid_status;

	return pipeline;

err_unexpected_attribute:
err_chan_is_busy:
err_chan_not_found:
err_invalid_status:
	{
	struct ks_chan *chan;
	struct ks_chan *chan2;
	write_lock_bh(&ks_connection_lock);
	list_for_each_entry_safe(chan, chan2, &pipeline->entries,
							pipeline_entry) {

		struct ks_pipeline *pipeline = chan->pipeline;

		chan->pipeline = NULL;
		list_del(&chan->pipeline_entry);
		ks_chan_put(chan);

		ks_pipeline_put(pipeline);
	}
	write_unlock_bh(&ks_connection_lock);
	}

	ks_pipeline_put(pipeline);
	pipeline = NULL;
err_pipeline_alloc:

	*errp = err;
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
		case KS_PIPELINEATTR_ID:
			BUG_ON(*(__u32 *)KS_ATTR_DATA(attr) != pipeline->id);
		break;

		case KS_PIPELINEATTR_STATUS:
			status = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		default:
			ks_msg(KERN_WARNING, "Unexpected attribute %d\n",
					attr->type);

			err = -EINVAL;
			goto err_unexpected_attribute;
		break;
		}
	}

	err = ks_pipeline_change_status(pipeline, status);
	if (err < 0)
		goto err_invalid_status;

	return 0;

err_unexpected_attribute:
err_invalid_status:

	return err;
}

int ks_pipeline_cmd_new(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;
	int err;

	pipeline = ks_pipeline_create_from_nlmsg(nlh, &err);
	if (!pipeline) {
		// FIXME
		return err;
	}

	if (debug_level > 1)
		ks_pipeline_dump(pipeline);

	err = ks_pipeline_register(pipeline);
	if (err < 0)
		goto err_pipeline_register;

	ks_pipeline_xact_write(pipeline, xact, KS_NETLINK_PIPELINE_NEW);

	ks_pipeline_register_bcast(pipeline);

	ks_pipeline_put(pipeline);

	return 0;

	ks_pipeline_unregister(pipeline);
err_pipeline_register:

	return err;
}

int ks_pipeline_cmd_del(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;
	int err;

	pipeline = ks_pipeline_get_by_nlid(nlh);
	if (!pipeline) {
		err = -ENOENT;
		goto err_pipeline_get;
	}

	if (pipeline->status != KS_PIPELINE_STATUS_NULL)
		ks_pipeline_change_status(pipeline, KS_PIPELINE_STATUS_NULL);

	ks_pipeline_unregister(pipeline);

	ks_pipeline_xact_write(pipeline, xact, KS_NETLINK_PIPELINE_DEL);

	ks_pipeline_put(pipeline);

	return 0;

	ks_pipeline_put(pipeline);
err_pipeline_get:

	return err;
}

int ks_pipeline_cmd_set(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;
	int err;

	pipeline = ks_pipeline_get_by_nlid(nlh);
	if (!pipeline) {
		err = -ENOENT;
		goto err_pipeline_get;
	}

	err = ks_pipeline_update_from_nlsmsg(pipeline, nlh);
	if (err < 0)
		goto err_pipeline_update;

	ks_pipeline_xact_write(pipeline, xact, KS_NETLINK_PIPELINE_SET);

	ks_pipeline_put(pipeline);

	return 0;

err_pipeline_update:
	ks_pipeline_put(pipeline);
err_pipeline_get:

	return err;
}

int ks_pipeline_cmd_get(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;
	int err;

	ks_xact_send_control(xact, KS_NETLINK_PIPELINE_GET,
			NLM_F_ACK | NLM_F_MULTI);

	list_for_each_entry(pipeline, &ks_pipelines_kset.list, kobj.entry) {

retry:
		ks_xact_need_skb(xact);
		if (!xact->out_skb)
			return -ENOMEM;

		err = ks_pipeline_write_to_nlmsg(pipeline, xact->out_skb,
					KS_NETLINK_PIPELINE_NEW,
					xact->pid,
					0,
					NLM_F_MULTI);
		if (err < 0) {
			ks_xact_flush(xact);
			goto retry;
		}
	}

	ks_xact_send_control(xact, NLMSG_DONE, NLM_F_MULTI);

	return 0;
}

struct ks_pipeline *ks_pipeline_create(struct ks_pipeline *pipeline)
{
	BUG_ON(pipeline); /* Static alloction not supported */

	if (!pipeline) {
		pipeline = kmalloc(sizeof(*pipeline), GFP_KERNEL);
		if (!pipeline)
			return NULL;
	}

	memset(pipeline, 0, sizeof(*pipeline));

	kobject_init(&pipeline->kobj);
	pipeline->kobj.kset = kset_get(&ks_pipelines_kset);

	INIT_LIST_HEAD(&pipeline->entries);

	pipeline->status = KS_PIPELINE_STATUS_NULL;

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

	/* NOTICE! netlink broadcast has to be explicitly sent with
	 * ks_pipeline_register_bcast fucntion */

	return 0;

	kobject_del(&pipeline->kobj);
err_kobject_add:
	down_write(&ks_pipelines_list_sem);
	list_del(&pipeline->node);
	ks_pipeline_put(pipeline);
	up_write(&ks_pipelines_list_sem);

	return err;
}

void ks_pipeline_register_bcast(struct ks_pipeline *pipeline)
{
	ks_pipeline_broadcast_netlink_notification(
			pipeline, KS_NETLINK_PIPELINE_NEW);

}

void ks_pipeline_unregister(struct ks_pipeline *pipeline)
{
	ks_pipeline_broadcast_netlink_notification(
			pipeline, KS_NETLINK_PIPELINE_DEL);

	ks_pipeline_change_status(pipeline, KS_PIPELINE_STATUS_NULL);

	kobject_del(&pipeline->kobj);

	down_write(&ks_pipelines_list_sem);
	list_del(&pipeline->node);
	ks_pipeline_put(pipeline);
	up_write(&ks_pipelines_list_sem);
}
EXPORT_SYMBOL(ks_pipeline_unregister);

void ks_pipeline_destroy(struct ks_pipeline *pipeline)
{
	ks_kobj_waitref(&pipeline->kobj);
	ks_pipeline_put(pipeline);
}
EXPORT_SYMBOL(ks_pipeline_destroy);

void ks_pipeline_dump(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;

	printk(KERN_DEBUG " ");

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		printk("%s/%s/%s =====(%s/%s/%s)=====> ",
			chan->from->kobj.parent->parent->name,
			chan->from->kobj.parent->name,
			chan->from->kobj.name,
			chan->kobj.parent->parent->name,
			chan->kobj.parent->name,
			chan->kobj.name);

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan)
		printk("%s/%s\n",
			prev_chan->to->kobj.parent->name,
			prev_chan->to->kobj.name);
}

static int ks_pipeline_negotiate_mtu(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	int min_mtu = 65536;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {
		if (chan->mtu != -1 &&
		    chan->mtu < min_mtu)
			min_mtu = chan->mtu;
	}
	read_unlock_bh(&ks_connection_lock);

	return min_mtu;
}

/*-------------------------- NULL <=> CONNECTED -----------------------------*/

static void ks_pipeline_connected_to_null(
	struct ks_pipeline *pipeline,
	struct ks_chan *stop_at)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan == stop_at) {
			read_unlock_bh(&ks_connection_lock);
			goto done;
		}

		if (chan->ops->disconnect)
			chan->ops->disconnect(chan);

		if (chan->from->ops->disconnect)
			chan->from->ops->disconnect(
				chan->from, chan, prev_chan);

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->disconnect)
		prev_chan->to->ops->disconnect(
			prev_chan->to, NULL, chan);

done:
	{
	struct ks_chan *chan2;
	write_lock_bh(&ks_connection_lock);
	list_for_each_entry_safe(chan, chan2, &pipeline->entries,
							pipeline_entry) {

		struct ks_pipeline *pipeline = chan->pipeline;

		WARN_ON(!chan->pipeline);

		chan->pipeline = NULL;

		list_del(&chan->pipeline_entry);
		ks_chan_put(chan);

		ks_pipeline_put(pipeline);
	}
	write_unlock_bh(&ks_connection_lock);
	}

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_NULL);
}

static int ks_pipeline_null_to_connected(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;
	int err;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan->from->ops->connect) {
			err = chan->from->ops->connect(
				chan->from, chan, prev_chan);
			if (err < 0) {
				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		if (chan->ops->connect) {
			err = chan->ops->connect(chan);
			if (err < 0) {
				if (chan->from->ops->disconnect)
					chan->from->ops->disconnect(chan->from,
							chan, prev_chan);

				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan &&
	    prev_chan->to->ops->connect)
		prev_chan->to->ops->connect(
			prev_chan->to, NULL, chan);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_CONNECTED);

	return 0;

failed:

	ks_pipeline_connected_to_null(pipeline, prev_chan);

	return err;
}

/*------------------------- CONNECTED <=> OPEN -----------------------------*/

static void ks_pipeline_open_to_connected(
	struct ks_pipeline *pipeline,
	struct ks_chan *stop_at)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan == stop_at) {
			read_unlock_bh(&ks_connection_lock);
			goto done;
		}

		if (chan->ops->close)
			chan->ops->close(chan);

		if (chan->from->ops->close)
			chan->from->ops->close(
				chan->from, chan, prev_chan);

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->close)
		prev_chan->to->ops->close(
			prev_chan->to, NULL, chan);

done:

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_CONNECTED);
}

static int ks_pipeline_connected_to_open(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;
	int err;

	pipeline->mtu = ks_pipeline_negotiate_mtu(pipeline);

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan->from->ops->open) {
			err = chan->from->ops->open(
				chan->from, chan, prev_chan);
			if (err < 0) {
				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		if (chan->ops->open) {
			err = chan->ops->open(chan);
			if (err < 0) {
				if (chan->from->ops->close)
					chan->from->ops->close(chan->from,
							chan, prev_chan);

				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->open)
		prev_chan->to->ops->open(
			prev_chan->to, NULL, chan);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_OPEN);

	return 0;

failed:

	ks_pipeline_open_to_connected(pipeline, prev_chan);

	return err;
}

/* -------------------------- OPEN <=> FLOWING -----------------------------*/

static void ks_pipeline_flowing_to_open(
	struct ks_pipeline *pipeline,
	struct ks_chan *stop_at)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan == stop_at) {
			read_unlock_bh(&ks_connection_lock);
			goto done;
		}

		if (chan->ops->stop)
			chan->ops->stop(chan);

		if (chan->from->ops->stop)
			chan->from->ops->stop(
				chan->from, chan, prev_chan);

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->stop)
		prev_chan->to->ops->stop(
			prev_chan->to, NULL, chan);

done:

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_OPEN);
}

static int ks_pipeline_open_to_flowing(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;
	int err;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan->from->ops->start) {
			err = chan->from->ops->start(
				chan->from, chan, prev_chan);
			if (err < 0) {
				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		if (chan->ops->start) {
			err = chan->ops->start(chan);
			if (err < 0) {
				if (chan->from->ops->stop)
					chan->from->ops->stop(chan->from,
							chan, prev_chan);

				read_unlock_bh(&ks_connection_lock);
				goto failed;
			}
		}

		prev_chan = chan;
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->start)
		prev_chan->to->ops->start(
			prev_chan->to, NULL, chan);

	ks_pipeline_set_status(pipeline, KS_PIPELINE_STATUS_FLOWING);

	return 0;

failed:

	ks_pipeline_flowing_to_open(pipeline, prev_chan);

	return err;
}

int ks_pipeline_change_status(
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
EXPORT_SYMBOL(ks_pipeline_change_status);

void ks_pipeline_stimulate(struct ks_pipeline *pipeline)
{
	struct ks_chan *chan;
	struct ks_chan *prev_chan = NULL;

	read_lock_bh(&ks_connection_lock);
	list_for_each_entry(chan, &pipeline->entries, pipeline_entry) {

		if (chan->ops->stimulus)
			chan->ops->stimulus(chan);

		if (chan->from->ops->stimulus)
			chan->from->ops->stimulus(
				chan->from, chan, prev_chan);
	}
	read_unlock_bh(&ks_connection_lock);

	if (prev_chan && prev_chan->to->ops->stimulus)
		prev_chan->to->ops->stimulus(
			prev_chan->to, NULL, chan);
}
EXPORT_SYMBOL(ks_pipeline_stimulate);

struct ks_chan *ks_pipeline_prev(struct ks_chan *chan)
{
	// FIXME LOCKING!!!

	if (!chan->pipeline)
		return NULL;

	if (chan->pipeline_entry.prev == &chan->pipeline->entries)
		return NULL;

	return list_entry(chan->pipeline_entry.prev, struct ks_chan,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_prev);

struct ks_chan *ks_pipeline_next(struct ks_chan *chan)
{

	if (!chan->pipeline)
		return NULL;

	if (chan->pipeline_entry.next == &chan->pipeline->entries)
		return NULL;

	return list_entry(chan->pipeline_entry.next, struct ks_chan,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_next);

struct ks_chan *ks_pipeline_first_chan(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.next == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.next, struct ks_chan,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_first_chan);

struct ks_chan *ks_pipeline_last_chan(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.prev == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.prev, struct ks_chan,
						pipeline_entry);
}
EXPORT_SYMBOL(ks_pipeline_last_chan);

struct ks_node *ks_pipeline_first_node(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.next == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.next, struct ks_chan,
						pipeline_entry)->from;
}
EXPORT_SYMBOL(ks_pipeline_first_node);

struct ks_node *ks_pipeline_last_node(struct ks_pipeline *pipeline)
{
	if (pipeline->entries.prev == &pipeline->entries)
		return NULL;

	return list_entry(pipeline->entries.prev, struct ks_chan,
						pipeline_entry)->to;
}
EXPORT_SYMBOL(ks_pipeline_last_node);

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

	ks_debug(3, "ks_pipeline_release()\n");

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	ks_pipelines_kset.subsys = &kstreamer_subsys;
#else
	ks_pipelines_kset.kobj.parent = &kstreamer_subsys.kobj;
#endif
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

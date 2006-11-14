/*
 * vISDN software crossconnector
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>

#include "softswitch.h"

struct vss_softswitch vss_softswitch;
EXPORT_SYMBOL(vss_softswitch);

#define to_vss_softswitch(ks_node)	\
		container_of((ks_node), struct vss_softswitch, ks_node)

int vss_chan_push_frame(struct ks_chan *chan, struct sk_buff *skb)
{
	struct ks_chan *to_chan;
	int res;

	BUG_ON(chan->to != &vss_softswitch.ks_node);

	rcu_read_lock();
	if (!chan->pipeline ||
	    chan->pipeline_entry.next == &chan->pipeline->entries) {
		rcu_read_unlock();
		return -ENOTCONN;
	}

	to_chan = list_entry(chan->pipeline_entry.next, struct ks_chan,
							pipeline_entry);

	if (!((struct vss_chan_ops *)to_chan->from_ops)->push_frame) {
		rcu_read_unlock();
		return -EOPNOTSUPP;
	}

	res = ((struct vss_chan_ops *)to_chan->from_ops)->
			push_frame(to_chan, skb);

	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(vss_chan_push_frame);

int vss_chan_push_raw(
	struct ks_chan *chan,
	struct ks_streamframe *sf)
{
	struct ks_chan *to_chan;
	int res;

	BUG_ON(chan->to != &vss_softswitch.ks_node);

	rcu_read_lock();
	if (!chan->pipeline ||
	    chan->pipeline_entry.next == &chan->pipeline->entries) {
		rcu_read_unlock();
		return -ENOTCONN;
	}

	to_chan = list_entry(chan->pipeline_entry.next, struct ks_chan,
							pipeline_entry);

	if (!((struct vss_chan_ops *)to_chan->from_ops)->push_raw) {
		rcu_read_unlock();
		return -EOPNOTSUPP;
	}

	res = ((struct vss_chan_ops *)to_chan->from_ops)->
			push_raw(to_chan, sf);

	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(vss_chan_push_raw);

int vss_chan_get_pressure(struct ks_chan *chan)
{
	struct ks_chan *to_chan;
	int res;

	BUG_ON(chan->to != &vss_softswitch.ks_node);

	rcu_read_lock();
	if (!chan->pipeline ||
	    chan->pipeline_entry.next == &chan->pipeline->entries) {
		rcu_read_unlock();
		return -ENOTCONN;
	}

	to_chan = list_entry(chan->pipeline_entry.next, struct ks_chan,
							pipeline_entry);

	if (!((struct vss_chan_ops *)to_chan->from_ops)->get_pressure) {
		rcu_read_unlock();
		return -EOPNOTSUPP;
	}

	res = ((struct vss_chan_ops *)to_chan->from_ops)->
			get_pressure(to_chan);

	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(vss_chan_get_pressure);

static void vss_release(struct ks_node *node)
{
	printk(KERN_DEBUG "vss_release()\n");
}

#if 0
static int vss_frame_xmit(
	struct ks_node *ks_node,
	struct visdn_leg *src_leg,
	struct sk_buff *skb)
{
	int res = -ENODEV;

	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->frame_xmit)
			res = dst->ops->frame_xmit(dst, skb);
		else
			printk(KERN_DEBUG
				"Chan %06d(%c) does not have a"
				" frame_xmit callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

		visdn_leg_put(dst);
	}

	return res;
}
	
static void vss_start_queue(
	struct ks_node *node,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->start_queue)
			dst->ops->start_queue(dst);
		else
			printk(KERN_DEBUG
				"Chan %06d(%c) does not have a"
				" start_queue callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

		visdn_leg_put(dst);
	}
}

static void vss_stop_queue(
	struct ks_node *node,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->stop_queue)
			dst->ops->stop_queue(dst);
		else
			printk(KERN_DEBUG
				"Chan %06d(%c) does not have a"
				" stop_queue callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

		visdn_leg_put(dst);
	}
}

static void vss_wake_queue(
	struct ks_node *node,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->wake_queue)
			dst->ops->wake_queue(dst);
		else
			printk(KERN_DEBUG
				"Chan %06d(%c) does not have a"
				" wake_queue callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

		visdn_leg_put(dst);
	}
}

static void vss_rx_error(
	struct ks_node *node,
	struct visdn_leg *src_leg,
	enum visdn_leg_rx_error_code code)
{
	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->rx_error)
			dst->ops->rx_error(dst, code);
		else {
			printk(KERN_ERR
				"Chan %06d(%c) does not have a"
				" rx_error callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

			dump_stack();
		}

		visdn_leg_put(dst);
	}
}

static void vss_tx_error(
	struct ks_node *node,
	struct visdn_leg *src_leg,
	enum visdn_leg_tx_error_code code)
{
	struct visdn_leg *dst;
	dst = ks_node_get_leg_by_src(src_leg->node, src_leg);

	if (dst) {
		if (dst->ops &&
		    dst->ops->tx_error)
			dst->ops->tx_error(dst, code);
		else
			printk(KERN_DEBUG
				"Chan %06d(%c) does not have a"
				" tx_error callback\n",
				dst->chan->id,
				dst->id ? 'b' : 'a');

		visdn_leg_put(dst);
	}
}
#endif 

struct ks_node_ops vss_ops =
{
	.owner		= THIS_MODULE,
	.release	= vss_release,
/*	.timer_func	= vss_timer_func,

	.frame_xmit	= vss_frame_xmit,

	.start_queue	= vss_start_queue,
	.stop_queue	= vss_stop_queue,
	.wake_queue	= vss_wake_queue,

	.rx_error	= vss_rx_error,
	.tx_error	= vss_tx_error,*/
};

static int __init vss_init_module(void)
{
	int err;

	memset(&vss_softswitch, 0, sizeof(vss_softswitch));

	ks_node_init(&vss_softswitch.ks_node,
			&vss_ops, "softswitch",
			NULL);

	err = ks_node_register(&vss_softswitch.ks_node);
	if (err < 0)
		goto err_ks_node_register;

	return 0;

	ks_node_unregister(&vss_softswitch.ks_node);
err_ks_node_register:

	return err;
}
module_init(vss_init_module);

static void __exit vss_modexit(void)
{
	ks_node_unregister(&vss_softswitch.ks_node);
}
module_exit(vss_modexit);

MODULE_DESCRIPTION(vss_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

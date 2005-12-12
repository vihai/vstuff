/*
 * vISDN software crossconnector
 *
 * Copyright (C) 2005 Daniele Orlandi
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

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>
#include <linux/visdn/cxc.h>

#include "softcxc.h"

struct vsc_softcxc vsc_softcxc;
EXPORT_SYMBOL(vsc_softcxc);

#define to_vsc_softcxc(visdn_cxc)	\
		container_of((visdn_cxc), struct vsc_softcxc, cxc)

static void vsc_timer_func(struct visdn_cxc *visdn_cxc)
{
	struct vsc_softcxc *cxc = to_vsc_softcxc(visdn_cxc);

	rcu_read_lock();

	{
	struct visdn_cxc_connection *cxc_conn;
	list_for_each_entry_rcu(cxc_conn,
			&cxc->cxc.connections_list,
			list_node) {

		struct visdn_leg *src = cxc_conn->src;
		struct visdn_leg *dst = cxc_conn->dst;

		if (test_bit(VISDN_CHAN_STATE_OPEN, &src->chan->state) &&
		    test_bit(VISDN_CHAN_STATE_OPEN, &dst->chan->state) &&
		    src->framing == VISDN_LEG_FRAMING_NONE &&
		    src->ops &&
		    src->ops->read) {
			int nread;
			nread = src->ops->read(cxc_conn->src, cxc->buf,
							sizeof(cxc->buf));

			if (dst->ops &&
			    dst->ops->write) {
				dst->ops->write(dst, cxc->buf, nread);
			}
		}
	}
	}

	rcu_read_unlock();
}

static void vsc_release(struct visdn_cxc *cxc)
{
	printk(KERN_DEBUG "vsc_release()\n");
}

static int vsc_frame_xmit(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg,
	struct sk_buff *skb)
{
	int res = -ENODEV;

	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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
	
static void vsc_start_queue(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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

static void vsc_stop_queue(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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

static void vsc_wake_queue(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg)
{
	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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

static void vsc_rx_error(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg,
	enum visdn_leg_rx_error_code code)
{
	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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

static void vsc_tx_error(
	struct visdn_cxc *cxc,
	struct visdn_leg *src_leg,
	enum visdn_leg_tx_error_code code)
{
	struct visdn_leg *dst;
	dst = visdn_cxc_get_leg_by_src(src_leg->cxc, src_leg);

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

struct visdn_cxc_ops vsc_ops =
{
	.owner		= THIS_MODULE,
	.release	= vsc_release,
	.timer_func	= vsc_timer_func,

	.frame_xmit	= vsc_frame_xmit,

	.start_queue	= vsc_start_queue,
	.stop_queue	= vsc_stop_queue,
	.wake_queue	= vsc_wake_queue,

	.rx_error	= vsc_rx_error,
	.tx_error	= vsc_tx_error,
};

static int __init vsc_init_module(void)
{
	int err;

	memset(&vsc_softcxc, 0, sizeof(vsc_softcxc));

	visdn_cxc_init(&vsc_softcxc.cxc);

	vsc_softcxc.cxc.ops = &vsc_ops;
	vsc_softcxc.cxc.name = "softcxc";

	err = visdn_cxc_register(&vsc_softcxc.cxc);
	if (err < 0)
		goto err_cxc_register;

	return 0;

	visdn_cxc_unregister(&vsc_softcxc.cxc);
err_cxc_register:

	return err;
}
module_init(vsc_init_module);

static void __exit vsc_modexit(void)
{
	visdn_cxc_unregister(&vsc_softcxc.cxc);
}
module_exit(vsc_modexit);

MODULE_DESCRIPTION(vsc_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

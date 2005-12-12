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

#define _GNU_SOURCE

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#include "core.h"
#include "chan.h"
#include "leg.h"
#include "cxc.h"

//#define verbose(...) do {} while(0)

#define verbose(fmt, arg...) printk(fmt, ## arg)
//#define DIJ_DEBUG

void visdn_router_print_node_name(struct visdn_router_node *node)
{
#ifdef DIJ_DEBUG
	if (node->is_channel)
		printk("channel[%06d]",
			container_of(node,
				struct visdn_chan, router_node)->id);
	else
		printk("cxc[%02d]",
			container_of(node,
				struct visdn_cxc, router_node)->id);
#endif
}

static struct list_head visdn_router_nodes =
			LIST_HEAD_INIT(visdn_router_nodes);
static struct list_head visdn_router_archs =
			LIST_HEAD_INIT(visdn_router_archs);

static DECLARE_MUTEX(visdn_router_sem);

void visdn_router_add_node(
	struct visdn_router_node *node)
{
	down(&visdn_router_sem);
	list_add_tail(&node->node, &visdn_router_nodes);
	up(&visdn_router_sem);
}

void visdn_router_del_node(
	struct visdn_router_node *node)
{
	down(&visdn_router_sem);
	list_del(&node->node);
	up(&visdn_router_sem);
}

void visdn_router_add_arch(
	struct visdn_router_arch *arch)
{
	down(&visdn_router_sem);
	list_add_tail(&arch->node, &visdn_router_archs);
	up(&visdn_router_sem);
}

void visdn_router_del_arch(
	struct visdn_router_arch *arch)
{
	down(&visdn_router_sem);
	list_del(&arch->node);
	up(&visdn_router_sem);
}

void visdn_router_run(
	struct visdn_router_node *start)
{
	{
	struct visdn_router_node *node;
	list_for_each_entry(node, &visdn_router_nodes, node) {
		node->cost = INT_MAX;
		node->done = FALSE;
		node->prev = NULL;
		node->prev_thru = NULL;
	}
	}

	start->cost = 0;

	while(1) {
		struct visdn_router_node *min_cost_node = NULL;
		struct visdn_router_node *node;
		list_for_each_entry(node, &visdn_router_nodes, node) {
			if (!node->done &&
			    (!min_cost_node ||
			    node->cost < min_cost_node->cost))
				min_cost_node = node;
		}

		if (!min_cost_node)
			break;

#ifdef DIJ_DEBUG
		verbose(KERN_DEBUG "Min cost (%d) node = ", min_cost_node->cost);
		visdn_router_print_node_name(min_cost_node);
		verbose("\n");
#endif

		min_cost_node->done = TRUE;

		/* For each arch exiting from node 'min_cost_node' */

		{
		struct visdn_router_arch *arch;
		list_for_each_entry(arch, &visdn_router_archs, node) {

			if (arch->src_node != min_cost_node)
				continue;

			if (arch->src_leg->cxc &&
			    visdn_cxc_leg_connected(
					arch->src_leg->cxc,
					arch->src_leg))
				continue;

#ifdef DIJ_DEBUG
			verbose(KERN_DEBUG "    Edge %06d from ",
				arch->src_leg->chan->id);
			visdn_router_print_node_name(min_cost_node);
			verbose(" to ");
			visdn_router_print_node_name(arch->dst_node);
			verbose(", cost = %d\n", arch->cost);
#endif

			if (arch->cost != INT_MAX &&
			    min_cost_node->cost != INT_MAX &&
			    arch->dst_node->cost > min_cost_node->cost +
						arch->cost) {

				arch->dst_node->cost =
					min_cost_node->cost +
					arch->cost;

				arch->dst_node->prev = min_cost_node;
				arch->dst_node->prev_thru = arch;

#ifdef DIJ_DEBUG
				verbose(KERN_DEBUG "        => Relaxing ");
				visdn_router_print_node_name(arch->dst_node);
				verbose(" new cost = %d\n",
					arch->dst_node->cost);
#endif
			}
		}
		}
	}
}

int visdn_connect_path(
	struct visdn_chan *src_chan,
	struct visdn_chan *dst_chan,
	struct file *bound_to_file)
{
	struct visdn_router_arch *prev_arch = NULL, *next_arch = NULL;
	struct visdn_router_node *node;
	int done_entries = 0;
	int err;

	if (src_chan->leg_a.cxc && src_chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			src_chan->id);
		return -EINVAL;
	}

	if (dst_chan->leg_a.cxc && dst_chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			dst_chan->id);
		return -EINVAL;
	}

	down(&visdn_router_sem);
	visdn_router_run(&src_chan->router_node);

	node = &dst_chan->router_node;
	while(node) {
		visdn_router_print_node_name(node);

		if (!node->prev_thru)
			break;
//		else
//			verbose(" ===(%06d)===> ", node->prev_thru->id);

		next_arch = node->prev_thru;

		if (prev_arch && next_arch) {
			WARN_ON(node->is_channel);

			err = visdn_cxc_connect(
				container_of(node, struct visdn_cxc,
							router_node),
				next_arch->src_leg->other_leg,
				prev_arch->src_leg,
				bound_to_file);
			if (err < 0)
				goto err_router_connect;

			done_entries++;
		}

		prev_arch = node->prev_thru;

		node = node->prev;
	}

	verbose("\n");

	up(&visdn_router_sem);

	return 0;

err_router_connect:
	{
	int i = 0;
	node = &dst_chan->router_node;
	while(node) {
		if (!node->prev_thru)
			break;

		next_arch = node->prev_thru;

		if (prev_arch && next_arch) {
			if (i >= done_entries)
				break;

			visdn_cxc_disconnect(
				container_of(node, struct visdn_cxc,
							router_node),
				next_arch->src_leg->other_leg,
				prev_arch->src_leg);

			i++;
		}
	}
	}

	up(&visdn_router_sem);

	return err;
}

int visdn_connect_path_with_id(
	int chan1_id,
	int chan2_id,
	struct file *bound_to_file)
{
	struct visdn_chan *chan1;
	struct visdn_chan *chan2;
	int err;

	chan1 = visdn_chan_get_by_id(chan1_id);
	if (!chan1) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan1_id);

		err = -ENODEV;
		goto err_search_src;
	}

	chan2 = visdn_chan_get_by_id(chan2_id);
	if (!chan2) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan2_id);

		err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_connect_self;
	}

	err = visdn_connect_path(chan1, chan2, bound_to_file);
	if (err < 0)
		goto err_connect;

	/* Release references returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

	return 0;

	// visdn_disconnect
err_connect:
err_connect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_connect_path_with_id);

int visdn_disconnect_path(
	struct visdn_chan *chan)
{
	struct visdn_leg *cur_leg, *next_leg;

	if (chan->leg_a.cxc && chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			chan->id);
		return -EINVAL;
	}

	if (chan->leg_a.cxc)
		cur_leg = &chan->leg_a;
	else
		cur_leg = &chan->leg_b;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		visdn_cxc_disconnect(cur_leg->cxc, cur_leg, next_leg);

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}

int visdn_disconnect_path_with_id(
	int chan_id)
{
	struct visdn_chan *chan;
	int err;

	chan = visdn_chan_get_by_id(chan_id);
	if (!chan) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan_id);

		err = -ENODEV;
		goto err_search_src;
	}

	err = visdn_disconnect_path(chan);
	if (err < 0)
		goto err_disconnect;

	/* Release reference returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan);

	return 0;

	// visdn_disconnect
err_disconnect:
	visdn_chan_put(chan);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_disconnect_path_with_id);

int visdn_enable_path(
	struct visdn_chan *chan)
{
	struct visdn_leg *cur_leg, *next_leg;
	int err;

	if (chan->leg_a.cxc && chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			chan->id);
		return -EINVAL;
	}

	err = visdn_chan_enable(chan);
	if (err < 0)
		return err;

	if (chan->leg_a.cxc)
		cur_leg = &chan->leg_a;
	else
		cur_leg = &chan->leg_b;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		err = visdn_chan_enable(next_leg->chan);
		if (err < 0) {
			visdn_leg_put(next_leg);
			visdn_leg_put(cur_leg);

			return err;
		}

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}
EXPORT_SYMBOL(visdn_enable_path);

int visdn_enable_path_with_id(
	int chan_id)
{
	struct visdn_chan *chan;
	int err;

	chan = visdn_chan_get_by_id(chan_id);
	if (!chan) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan_id);

		err = -ENODEV;
		goto err_search_src;
	}

	err = visdn_enable_path(chan);
	if (err < 0)
		goto err_enable;

	/* Release reference returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan);

	return 0;

err_enable:
	visdn_chan_put(chan);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_enable_path_with_id);

int visdn_disable_path(
	struct visdn_chan *chan)
{
	struct visdn_leg *cur_leg, *next_leg;

	if (chan->leg_a.cxc && chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			chan->id);
		return -EINVAL;
	}

	visdn_chan_disable(chan);

	if (chan->leg_a.cxc)
		cur_leg = &chan->leg_a;
	else
		cur_leg = &chan->leg_b;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		visdn_chan_disable(next_leg->chan);

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}
EXPORT_SYMBOL(visdn_disable_path);

int visdn_disable_path_with_id(
	int chan_id)
{
	struct visdn_chan *chan;
	int err;

	chan = visdn_chan_get_by_id(chan_id);
	if (!chan) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan_id);

		err = -ENODEV;
		goto err_search_src;
	}

	err = visdn_disable_path(chan);
	if (err < 0)
		goto err_disable;

	/* Release reference returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan);

	return 0;

	// visdn_disable
err_disable:
	visdn_chan_put(chan);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_disable_path_with_id);

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

#define _GNU_SOURCE

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#include "core.h"
#include "chan.h"
#include "leg.h"
#include "cxc.h"
#include "pipeline.h"
#include "router.h"

//#define verbose(...) do {} while(0)

#define verbose(fmt, arg...) printk(fmt, ## arg)
//#define DIJ_DEBUG
//
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
		verbose(KERN_DEBUG
			"Min cost (%d) node = ",
			min_cost_node->cost);
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

void visdn_router_lock()
{
	down(&visdn_router_sem);
}

void visdn_router_unlock()
{
	up(&visdn_router_sem);
}

int visdn_router_modinit()
{
	return 0;
}

void visdn_router_modexit()
{
	WARN_ON(!list_empty(&visdn_router_nodes));
	WARN_ON(!list_empty(&visdn_router_archs));
}

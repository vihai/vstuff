/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/netlink.h>

#include <libkstreamer/util.h>
#include <libkstreamer/router.h>
#include <libkstreamer/node.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/pipeline.h>
#include <libkstreamer/conn.h>
#include <libkstreamer/logging.h>

#define DIJ_DEBUG

void ks_router_run(
	struct ks_node *start,
	struct ks_node *to)
{
	int i;
	struct ks_node *node;

	assert(start);
	assert(start->conn);
	assert(to->conn);
	assert(start->conn == to->conn);

	struct ks_conn *conn = start->conn;

	for (i=0; i<ARRAY_SIZE(conn->nodes_hash); i++) {
		struct hlist_node *t;

		hlist_for_each_entry(node, t, &conn->nodes_hash[i], node) {
			node->router_cost = INT_MAX;
			node->router_visited = FALSE;
			node->router_prev = NULL;
			node->router_prev_thru = NULL;
		}
	}

	start->router_cost = 0;

	while(1) {
		struct ks_node *min_cost_node = NULL;

		/* Extract the node with lowest cost */
		for (i=0; i<ARRAY_SIZE(conn->nodes_hash); i++) {
			struct ks_node *node;
			struct hlist_node *t;

			hlist_for_each_entry(node, t, &conn->nodes_hash[i],
									node) {
				if (!node->router_visited &&
				    (!min_cost_node ||
				    node->router_cost <
						min_cost_node->router_cost))
					min_cost_node = node;
			}
		}

		if (!min_cost_node)
			break;

		if (min_cost_node == to)
			break;

		debug_conn(conn, debug_router,
			"Min cost (%d) node = %s\n",
			min_cost_node->router_cost,
			min_cost_node->path);

		min_cost_node->router_visited = TRUE;

		/* For each arch exiting from node 'min_cost_node' */

		struct ks_chan *arch;
		for (i=0; i<ARRAY_SIZE(conn->chans_hash); i++) {
			struct hlist_node *t;

			hlist_for_each_entry(arch, t, &conn->chans_hash[i],
								node) {

				if (arch->from != min_cost_node)
					continue;

				if (arch->pipeline)
					continue;

				debug_conn(conn, debug_router,
					"    Arch (%s) from"
					" node (%s) to node (%s),"
					" cost = %d\n",
					arch->path,
					arch->from->path,
					arch->to->path,
					arch->cost);

				if (arch->cost != INT_MAX &&
				    min_cost_node->router_cost != INT_MAX &&
				    arch->to->router_cost >
					min_cost_node->router_cost +
						arch->cost) {

					arch->to->router_cost =
						min_cost_node->router_cost +
						arch->cost;

					arch->to->router_prev = min_cost_node;
					arch->to->router_prev_thru = arch;

					debug_conn(conn, debug_router,
						"        => Relaxing node (%s)"
						" new cost = %d\n",
						arch->to->path,
						arch->to->router_cost);
			}
		}
		}
	}
}

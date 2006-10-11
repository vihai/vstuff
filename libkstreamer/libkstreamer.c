/*
 * Socket Buffer Userland Implementation
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#define _GNU_SOURCE
#define _LIBKSTREAMER_PRIVATE_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <libskb.h>

#include "libkstreamer.h"
#include "netlink.h"
#include "link.h"
#include "pipeline.h"
#include "util.h"
//#include "node.h"
//
void ks_update_topology(struct ks_conn *conn)
{
	conn->state = KS_STATE_SYNCING;

	ks_send_topology_update_req(conn);

	ks_netlink_waitloop(conn);
}

struct ks_pipeline *ks_connect(
	struct ks_conn *conn,
	struct ks_node *src_node,
	struct ks_node *dst_node,
	int *err)
{
	int pipeline_cnt = 0;

	router_run(src_node, dst_node);

	struct ks_node *node;
	for(node = dst_node; node->router_prev;
	    node = node->router_prev, pipeline_cnt++);

	if (node != src_node) {
		if (err)
			*err = -EHOSTUNREACH;

		return NULL;
	}

	struct ks_pipeline *pipeline = ks_pipeline_alloc();
	if (!pipeline) {
		if (err)
			*err = -ENOMEM;

		return NULL;
	}

	int i;
	for(node = dst_node, i=0; node->router_prev;
	    node = node->router_prev, i++) {
		pipeline->links[pipeline_cnt - i - 1] =
			node->router_prev_thru;
	}

	pipeline->links_cnt = pipeline_cnt;

	return pipeline;
}


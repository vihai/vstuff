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
#include "req.h"
#include "channel.h"
#include "pipeline.h"
#include "util.h"
#include "xact.h"

void ks_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	switch(nlh->nlmsg_type) {
	case NLMSG_NOOP:
	case NLMSG_OVERRUN:
	case NLMSG_ERROR:
	case NLMSG_DONE:
	break;

	case KS_NETLINK_ABORT:
	case KS_NETLINK_COMMIT:
		fprintf(stderr, "Unexpected COMMIT/ABORT message\n");
	break;

	case KS_NETLINK_DYNATTR_NEW: {
		struct ks_dynattr *dynattr;
		dynattr = ks_dynattr_create_from_nlmsg(nlh);
		if (!dynattr) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_add(dynattr);
		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_DYNATTR_DEL: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(nlh);
		if (!dynattr) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_del(dynattr);
		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_DYNATTR_SET: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(nlh);
		if (!dynattr) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_dynattr_update_from_nlmsg(dynattr, nlh);

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_NODE_NEW: {
		struct ks_node *node;

		node = ks_node_create_from_nlmsg(nlh);
		if (!node) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_add(node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_DEL: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(nlh);
		if (!node) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_del(node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_SET: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(nlh);
		if (!node) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_node_update_from_nlmsg(node, nlh);

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_put(node);
	}
	break;

	case KS_NETLINK_CHAN_NEW: {
		struct ks_chan *chan;

		chan = ks_chan_create_from_nlmsg(nlh);
		if (!chan) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_chan_dump(chan);

		ks_chan_add(chan); // CHECK FOR DUPEs
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_DEL: {
		struct ks_chan *chan;

		chan = ks_chan_get_by_nlid(nlh);
		if (!chan) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_chan_dump(chan);

		ks_chan_del(chan);
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_SET: {
		struct ks_chan *chan;

		chan = ks_chan_get_by_nlid(nlh);
		if (!chan) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_chan_update_from_nlmsg(chan, nlh);

		if (conn->dump_packets)
			ks_chan_dump(chan);

		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_PIPELINE_NEW: {
		struct ks_pipeline *pipeline;
		pipeline = ks_pipeline_create_from_nlmsg(nlh);
		if (!pipeline) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_add(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_DEL: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(nlh);
		if (!pipeline) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_del(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_SET: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(nlh);
		if (!pipeline) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_pipeline_update_from_nlmsg(pipeline, nlh);

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_put(pipeline);
	}
	break;
	}
}

static int ks_topology_callback(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_xact *xact = req->xact;
	struct ks_conn *conn = xact->conn;

	ks_topology_update(conn, nlh);

	return 0;
}

struct ks_xact *ks_send_topology_update_req(
	struct ks_conn *conn, int *errp)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_xact_queue_begin(xact);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_begin;
	}
	ks_req_put(req);

	req = ks_xact_queue_new_request_callback(xact,
			KS_NETLINK_DYNATTR_GET,
			NLM_F_REQUEST | NLM_F_ROOT,
			ks_topology_callback);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_queue_new_request;
	}
	ks_req_put(req);

	req = ks_xact_queue_new_request_callback(xact,
			KS_NETLINK_NODE_GET,
			NLM_F_REQUEST | NLM_F_ROOT,
			ks_topology_callback);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_queue_new_request;
	}
	ks_req_put(req);

	req = ks_xact_queue_new_request_callback(xact,
			KS_NETLINK_CHAN_GET,
			NLM_F_REQUEST | NLM_F_ROOT,
			ks_topology_callback);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_queue_new_request;
	}
	ks_req_put(req);

	req = ks_xact_queue_new_request_callback(xact,
			KS_NETLINK_PIPELINE_GET,
			NLM_F_REQUEST | NLM_F_ROOT,
			ks_topology_callback);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_queue_new_request;
	}
	ks_req_put(req);

	req = ks_xact_queue_commit(xact);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_xact_commit;
	}
	ks_req_put(req);

	ks_xact_run(xact);

	return xact;

err_xact_begin:
err_xact_queue_new_request:
err_xact_commit:
	ks_xact_put(xact);
err_xact_alloc:

	*errp = err;
	return NULL;
}

void ks_update_topology(struct ks_conn *conn)
{
	int err;

	conn->topology_state = KS_TOPOLOGY_STATE_SYNCING;

	struct ks_xact *xact;
	xact = ks_send_topology_update_req(conn, &err);

	ks_xact_wait(xact);
	ks_xact_put(xact);
}

#if 0
int ks_send_noop(struct ks_conn *conn)
{
	int err;

	struct sk_buff *skb;
	skb = skb_alloc(NLMSG_SPACE(0), 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	ks_nlmsg_put(skb, getpid(), req->id, NLMSG_NOOP, NLM_F_REQUEST, 0);

	ks_req_send(req, conn, skb);
	ks_req_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

	ks_req_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
}
#endif


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
		pipeline->chans[pipeline_cnt - i - 1] =
			node->router_prev_thru;
	}

	pipeline->chans_cnt = pipeline_cnt;

	return pipeline;
}


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
#include <errno.h>

#include <libskb.h>

#include "libkstreamer.h"
#include "netlink.h"
#include "req.h"
#include "channel.h"
#include "pipeline.h"
#include "util.h"
#include "xact.h"
#include "logging.h"

void ks_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	pthread_mutex_lock(&conn->topology_lock);

	switch(nlh->nlmsg_type) {
	case NLMSG_NOOP:
	case NLMSG_OVERRUN:
	case NLMSG_ERROR:
	case NLMSG_DONE:
	break;

	case KS_NETLINK_ABORT:
	case KS_NETLINK_COMMIT:
		report_conn(conn, LOG_ERR, "Unexpected COMMIT/ABORT message\n");
	break;

	case KS_NETLINK_DYNATTR_NEW: {
		struct ks_dynattr *dynattr;
		dynattr = ks_dynattr_create_from_nlmsg(conn, nlh);
		if (!dynattr) {
			// FIXME
		}

		if (conn->debug_netlink)
			ks_dynattr_dump(dynattr, conn, LOG_DEBUG);

		ks_dynattr_add(dynattr, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, dynattr);
		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_DYNATTR_DEL: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(conn, nlh);
		if (!dynattr) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		if (conn->debug_netlink)
			ks_dynattr_dump(dynattr, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, dynattr);
		ks_dynattr_del(dynattr);
		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_DYNATTR_SET: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(conn, nlh);
		if (!dynattr) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		ks_dynattr_update_from_nlmsg(dynattr, conn, nlh);

		if (conn->debug_netlink)
			ks_dynattr_dump(dynattr, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, dynattr);

		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_NODE_NEW: {
		struct ks_node *node;

		node = ks_node_create_from_nlmsg(conn, nlh);
		if (!node) {
			// FIXME
		}

		if (conn->debug_netlink)
			ks_node_dump(node, conn, LOG_DEBUG);

		ks_node_add(node, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_DEL: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(conn, nlh);
		if (!node) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		if (conn->debug_netlink)
			ks_node_dump(node, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);
		ks_node_del(node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_SET: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(conn, nlh);
		if (!node) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		ks_node_update_from_nlmsg(node, conn, nlh);

		if (conn->debug_netlink)
			ks_node_dump(node, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);

		ks_node_put(node);
	}
	break;

	case KS_NETLINK_CHAN_NEW: {
		struct ks_chan *chan;

		chan = ks_chan_create_from_nlmsg(conn, nlh);
		if (!chan) {
			// FIXME
		}

		if (conn->debug_netlink)
			ks_chan_dump(chan, conn, LOG_DEBUG);

		ks_chan_add(chan, conn); // CHECK FOR DUPEs
		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_DEL: {
		struct ks_chan *chan;

		chan = ks_chan_get_by_nlid(conn, nlh);
		if (!chan) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		if (conn->debug_netlink)
			ks_chan_dump(chan, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);
		ks_chan_del(chan);
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_SET: {
		struct ks_chan *chan;

		chan = ks_chan_get_by_nlid(conn, nlh);
		if (!chan) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		ks_chan_update_from_nlmsg(chan, conn, nlh);

		if (conn->debug_netlink)
			ks_chan_dump(chan, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);

		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_PIPELINE_NEW: {
		struct ks_pipeline *pipeline;
		pipeline = ks_pipeline_create_from_nlmsg(conn, nlh);
		if (!pipeline) {
			// FIXME
		}

		if (conn->debug_netlink)
			ks_pipeline_dump(pipeline, conn, LOG_DEBUG);

		ks_pipeline_add(pipeline, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_DEL: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(conn, nlh);
		if (!pipeline) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		if (conn->debug_netlink)
			ks_pipeline_dump(pipeline, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);
		ks_pipeline_del(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_SET: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(conn, nlh);
		if (!pipeline) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		ks_pipeline_update_from_nlmsg(pipeline, conn, nlh);

		if (conn->debug_netlink)
			ks_pipeline_dump(pipeline, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);

		ks_pipeline_put(pipeline);
	}
	break;
	}

	pthread_mutex_unlock(&conn->topology_lock);
}

static int ks_topology_callback(
	struct ks_req *req,
	struct nlmsghdr *nlh)
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

	ks_xact_submit(xact);

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

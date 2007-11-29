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
#include "feature.h"
#include "channel.h"
#include "pipeline.h"
#include "util.h"
#include "xact.h"
#include "logging.h"

pthread_mutex_t refcnt_lock = PTHREAD_MUTEX_INITIALIZER;

void ks_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	pthread_rwlock_wrlock(&conn->topology_lock);

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

	case KS_NETLINK_FEATURE_NEW:
	case KS_NETLINK_FEATURE_DEL:
	case KS_NETLINK_FEATURE_SET:
		ks_feature_handle_topology_update(conn, nlh);
	break;

	case KS_NETLINK_NODE_NEW:
	case KS_NETLINK_NODE_DEL:
	case KS_NETLINK_NODE_SET:
		ks_node_handle_topology_update(conn, nlh);
	break;

	case KS_NETLINK_CHAN_NEW:
	case KS_NETLINK_CHAN_DEL:
	case KS_NETLINK_CHAN_SET:
		ks_chan_handle_topology_update(conn, nlh);
	break;

	case KS_NETLINK_PIPELINE_NEW:
	case KS_NETLINK_PIPELINE_DEL:
	case KS_NETLINK_PIPELINE_SET:
		ks_pipeline_handle_topology_update(conn, nlh);
	break;
	}

	pthread_rwlock_unlock(&conn->topology_lock);
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
			KS_NETLINK_FEATURE_GET,
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
	ks_xact_queue_abort(xact);
err_xact_commit:
	ks_xact_put(xact);
err_xact_alloc:

	*errp = err;
	return NULL;
}

void ks_update_topology(struct ks_conn *conn)
{
	int err;

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_SYNCING);

	ks_pipeline_flush(conn);
	ks_chan_flush(conn);
	ks_node_flush(conn);
	ks_feature_flush(conn);
	pthread_rwlock_unlock(&conn->topology_lock);

	struct ks_xact *xact;
	xact = ks_send_topology_update_req(conn, &err);

	ks_xact_wait(xact);
	ks_xact_put(xact);

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_SYNCHED);
	pthread_rwlock_unlock(&conn->topology_lock);
}

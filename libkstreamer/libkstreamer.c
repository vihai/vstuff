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

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/req.h>
#include <libkstreamer/feature.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/pipeline.h>
#include <libkstreamer/util.h>
#include <libkstreamer/logging.h>

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

	case KS_NETLINK_TOPOLOGY_LOCK:
	case KS_NETLINK_TOPOLOGY_TRYLOCK:
	case KS_NETLINK_TOPOLOGY_UNLOCK:
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

int ks_update_topology(struct ks_conn *conn)
{
	int err;

	err = ks_conn_remote_topology_lock(conn);
	if (err < 0)
		goto err_lock_failed;

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_SYNCING);

	ks_pipeline_flush(conn);
	ks_chan_flush(conn);
	ks_node_flush(conn);
	ks_feature_flush(conn);
	pthread_rwlock_unlock(&conn->topology_lock);

	/*------- Queue features request -------*/

	struct ks_req *req_feature;
	req_feature = ks_req_alloc(conn);
	if (!req_feature) {
		err = -ENOMEM;
		goto err_req_alloc_feature;
	}

	req_feature->type = KS_NETLINK_FEATURE_GET;
	req_feature->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req_feature);

	/*------- Queue nodes request -------*/

	struct ks_req *req_node;
	req_node = ks_req_alloc(conn);
	if (!req_node) {
		err = -ENOMEM;
		goto err_req_alloc_node;
	}

	req_node->type = KS_NETLINK_NODE_GET;
	req_node->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req_node);

	/*------- Queue channels request -------*/

	struct ks_req *req_chan;
	req_chan = ks_req_alloc(conn);
	if (!req_chan) {
		err = -ENOMEM;
		goto err_req_alloc_chan;
	}

	req_chan->type = KS_NETLINK_CHAN_GET;
	req_chan->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req_chan);

	/*------- Queue pipelines request -------*/

	struct ks_req *req_pipeline;
	req_pipeline = ks_req_alloc(conn);
	if (!req_pipeline) {
		err = -ENOMEM;
		goto err_req_alloc_pipeline;
	}

	req_pipeline->type = KS_NETLINK_PIPELINE_GET;
	req_pipeline->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req_pipeline);

	/*---------------------------------------*/

	ks_conn_flush_requests(conn);

	/*------ Wait and process features ------*/

	ks_req_wait(req_feature);
	if (req_feature->err < 0) {
		err = req_feature->err;
		ks_req_put(req_feature);
		goto err_req_result_feature;
	}

	{
	struct nlmsghdr *nlh;
	int len_left = req_feature->response_payload_size;

	for (nlh = req_feature->response_payload;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_feature_handle_topology_update(conn, nlh);
	}

	ks_req_put(req_feature);

	/*------ Wait and process nodes ------*/

	ks_req_wait(req_node);
	if (req_node->err < 0) {
		err = req_node->err;
		ks_req_put(req_node);
		goto err_req_result_node;
	}

	{
	struct nlmsghdr *nlh;
	int len_left = req_node->response_payload_size;

	for (nlh = req_node->response_payload;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_node_handle_topology_update(conn, nlh);
	}

	ks_req_put(req_node);

	/*------ Wait and process channels ------*/

	ks_req_wait(req_chan);
	if (req_chan->err < 0) {
		err = req_chan->err;
		ks_req_put(req_chan);
		goto err_req_result_chan;
	}

	{
	struct nlmsghdr *nlh;
	int len_left = req_chan->response_payload_size;

	for (nlh = req_chan->response_payload;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_chan_handle_topology_update(conn, nlh);
	}

	ks_req_put(req_chan);

	/*------ Wait and process pipelines ------*/

	ks_req_wait(req_pipeline);
	if (req_pipeline->err < 0) {
		err = req_pipeline->err;
		ks_req_put(req_pipeline);
		goto err_req_result_pipeline;
	}

	{
	struct nlmsghdr *nlh;
	int len_left = req_pipeline->response_payload_size;

	for (nlh = req_pipeline->response_payload;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_pipeline_handle_topology_update(conn, nlh);
	}

	ks_req_put(req_pipeline);

	/*---------------------------------------*/

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_SYNCHED);
	pthread_rwlock_unlock(&conn->topology_lock);

	err = ks_conn_remote_topology_unlock(conn);
	if (err < 0)
		goto err_unlock_failed;

	return 0;

err_unlock_failed:
err_req_result_pipeline:
err_req_result_chan:
err_req_result_node:
err_req_result_feature:
	ks_req_put(req_pipeline);
err_req_alloc_pipeline:
	ks_req_put(req_chan);
err_req_alloc_chan:
	ks_req_put(req_node);
err_req_alloc_node:
	ks_req_put(req_feature);
err_req_alloc_feature:
	ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_INVALID);
	ks_conn_remote_topology_unlock(conn);
err_lock_failed:

	return err;

}

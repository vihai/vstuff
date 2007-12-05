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
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/pipeline.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/node.h>
#include <libkstreamer/feature.h>
#include <libkstreamer/req.h>
#include <libkstreamer/xact.h>
#include <libkstreamer/router.h>
#include <libkstreamer/logging.h>

static inline struct hlist_head *ks_pipeline_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->pipelines_hash[id & (PIPELINE_HASHSIZE - 1)];
}

void ks_pipeline_add(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	pipeline->conn = conn;

	hlist_add_head(
		&ks_pipeline_get(pipeline)->node,
		ks_pipeline_get_hash(conn, pipeline->id));
}

void ks_pipeline_del(struct ks_pipeline *pipeline)
{
	hlist_del(&pipeline->node);

	int i;
	for (i=0; i<pipeline->chans_cnt; i++)
		pipeline->chans[i]->pipeline = NULL;

	ks_pipeline_put(pipeline);
}

void ks_pipeline_flush(struct ks_conn *conn)
{
	struct hlist_node *pos, *n;
	struct ks_pipeline *pipeline;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->pipelines_hash); i++) {
		hlist_for_each_entry_safe(pipeline, pos, n,
					&conn->pipelines_hash[i], node) {

			hlist_del(&pipeline->node);
			ks_pipeline_put(pipeline);
		}
	}
}

static struct ks_pipeline *_ks_pipeline_get_by_id(
	struct ks_conn *conn,
	int id)
{
	struct ks_pipeline *pipeline;
	struct hlist_node *t;

	hlist_for_each_entry(pipeline, t, ks_pipeline_get_hash(conn, id),
								node) {
		if (pipeline->id == id)
			return ks_pipeline_get(pipeline);
	}

	return NULL;
}

struct ks_pipeline *ks_pipeline_get_by_id(
	struct ks_conn *conn,
	int id)
{
	struct ks_pipeline *pipeline;

	pthread_rwlock_rdlock(&conn->topology_lock);
	pipeline = _ks_pipeline_get_by_id(conn, id);
	pthread_rwlock_unlock(&conn->topology_lock);

	return pipeline;
}

static struct ks_pipeline *_ks_pipeline_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	struct ks_pipeline *pipeline;
	struct hlist_node *t;

	int i;
	for(i=0; i<ARRAY_SIZE(conn->pipelines_hash); i++) {
		hlist_for_each_entry(pipeline, t, &conn->pipelines_hash[i],
								node) {
			if (!strcmp(pipeline->path, path))
				return ks_pipeline_get(pipeline);
		}
	}

	return NULL;
}

struct ks_pipeline *ks_pipeline_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	struct ks_pipeline *pipeline;

	pthread_rwlock_rdlock(&conn->topology_lock);
	pipeline = _ks_pipeline_get_by_path(conn, path);
	pthread_rwlock_unlock(&conn->topology_lock);

	return pipeline;
}

static struct ks_pipeline *_ks_pipeline_get_by_string(
	struct ks_conn *conn,
	const char *pipeline_str)
{
	struct ks_pipeline *pipeline;

	if (pipeline_str[0] == '/') {
		char *real_path;

		real_path = realpath(pipeline_str, NULL);
		if (!real_path) {
			report_conn(conn, LOG_WARNING,
				"Cannot resolve path '%s': %s\n",
				pipeline_str, strerror(errno));
			return NULL;
		}

		pipeline = _ks_pipeline_get_by_path(conn, real_path + 4);
		free(real_path);
	} else
		pipeline = _ks_pipeline_get_by_id(conn, atoi(pipeline_str));

	return pipeline;
}

struct ks_pipeline *ks_pipeline_get_by_string(
	struct ks_conn *conn,
	const char *pipeline_str)
{
	struct ks_pipeline *pipeline;

	pthread_rwlock_rdlock(&conn->topology_lock);
	pipeline = _ks_pipeline_get_by_string(conn, pipeline_str);
	pthread_rwlock_unlock(&conn->topology_lock);

	return pipeline;
}

static struct ks_pipeline *_ks_pipeline_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_PIPELINEATTR_ID)
			return _ks_pipeline_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

const char *ks_netlink_pipeline_attr_to_string(
		enum ks_pipeline_attribute_type type)
{
	switch(type) {
	case KS_PIPELINEATTR_ID:
		return "ID";
	case KS_PIPELINEATTR_PATH:
		return "Path";
	case KS_PIPELINEATTR_STATUS:
		return "Status";
	case KS_PIPELINEATTR_CHAN_ID:
		return "Chan ID";
	}

	return "UNKNOWN";
}

struct ks_pipeline *ks_pipeline_alloc(void)
{
	struct ks_pipeline *pipeline;

	pipeline = malloc(sizeof(*pipeline));
	if (!pipeline)
		return NULL;

	memset(pipeline, 0, sizeof(*pipeline));

	pipeline->refcnt = 1;

	return pipeline;
}

struct ks_pipeline *ks_pipeline_get(struct ks_pipeline *pipeline)
{
	assert(pipeline->refcnt > 0);

	if (pipeline) {
		pthread_mutex_lock(&refcnt_lock);
		pipeline->refcnt++;
		pthread_mutex_unlock(&refcnt_lock);
	}

	return pipeline;
}

void ks_pipeline_put(struct ks_pipeline *pipeline)
{
	assert(pipeline->refcnt > 0);

	pthread_mutex_lock(&refcnt_lock);
	int refcnt = --pipeline->refcnt;
	pthread_mutex_unlock(&refcnt_lock);

	if (!refcnt) {
		if (pipeline->path)
			free(pipeline->path);

		int i;
		for(i=0; i<pipeline->chans_cnt; i++)
			ks_chan_put(pipeline->chans[i]);

		free(pipeline);
	}
}

static const char *ks_pipeline_status_to_string(
		enum ks_pipeline_status status) {

	switch(status) {
	case KS_PIPELINE_STATUS_NULL:
		return "NULL";
	case KS_PIPELINE_STATUS_CONNECTED:
		return "CONNECTED";
	case KS_PIPELINE_STATUS_OPEN:
		return "OPEN";
	case KS_PIPELINE_STATUS_FLOWING:
		return "FLOWING";
	}

	return "*INVALID*";
}

void ks_pipeline_dump(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn,
	int level)
{
	report_conn(conn, level, "  ID    : 0x%08x\n", pipeline->id);
	report_conn(conn, level, "  Path  : '%s'\n", pipeline->path);
	report_conn(conn, level, "  Status: %s\n",
		ks_pipeline_status_to_string(pipeline->status));

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {

		struct ks_chan *chan = pipeline->chans[i];

		report_conn(conn, level, "%s =>(%s)=> %s\n",
			chan->from->path,
			chan->path,
			chan->to->path);

		struct ks_feature_value *featval;
		list_for_each_entry(featval, &chan->features, node) {
			report_conn(conn, level,
				"  Feature: %s\n", featval->feature->name);
		}
	}
}

static struct ks_pipeline *ks_pipeline_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;

	pipeline = ks_pipeline_alloc();
	if (!pipeline)
		return NULL;

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_PIPELINEATTR_ID:
			pipeline->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_STATUS:
			pipeline->status =
			        *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_PATH:
			pipeline->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_PIPELINEATTR_CHAN_ID: {
			struct ks_chan *chan;
			chan = _ks_chan_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
			if (!chan) {
				report_conn(conn, LOG_ERR,
					"Channel 0x%08x not found!\n",
					*(__u32 *)KS_ATTR_DATA(attr));
				break;
			}

			pipeline->chans[pipeline->chans_cnt] =
							ks_chan_get(chan);
			pipeline->chans_cnt++;

			chan->pipeline = pipeline;
		}
		break;

		default:
			report_conn(conn, LOG_ERR, "   Attribute '%s'\n",
				ks_netlink_pipeline_attr_to_string(
					attr->type));
		}
	}

	return pipeline;
}

static void ks_pipeline_update_from_nlmsg(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_PIPELINEATTR_ID:
			pipeline->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_STATUS:
			pipeline->status =
			        *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_PIPELINEATTR_PATH:
			if (pipeline->path)
				free(pipeline->path);

			pipeline->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_PIPELINEATTR_CHAN_ID:
			// Not supported
		break;

		default:
			report_conn(conn, LOG_ERR, "   Attribute '%s'\n",
				ks_netlink_pipeline_attr_to_string(
					attr->type));
		}
	}
}

void ks_pipeline_handle_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	switch(nlh->nlmsg_type) {
	case KS_NETLINK_PIPELINE_NEW: {
		struct ks_pipeline *pipeline;

		pipeline = _ks_pipeline_get_by_nlid(conn, nlh);
		if (pipeline) {
			ks_pipeline_put(pipeline);
			break;
		}

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

		pipeline = _ks_pipeline_get_by_nlid(conn, nlh);
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

		pipeline = _ks_pipeline_get_by_nlid(conn, nlh);
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
}

static int ks_pipeline_create_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline = req->response_data;
	struct ks_conn *conn = req->xact->conn;

	ks_pipeline_update_from_nlmsg(pipeline, conn, nlh);

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_pipeline_add(pipeline, conn);
	pthread_rwlock_unlock(&conn->topology_lock);

	ks_pipeline_put(pipeline);

	return 0;
}

struct ks_req *ks_pipeline_queue_create(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		goto err_req_alloc;

	req->type = KS_NETLINK_PIPELINE_NEW;
	req->flags = NLM_F_REQUEST;
	req->response_callback = ks_pipeline_create_handle_response;
	req->response_data = ks_pipeline_get(pipeline);

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb)
		goto err_skb_alloc;

	int err;

	err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_STATUS,
			&pipeline->status,
			sizeof(pipeline->status));
	if (err < 0)
		goto err_put_attr_status;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_CHAN_ID,
				&pipeline->chans[i]->id,
				sizeof(pipeline->chans[i]->id));
		if (err < 0)
			goto err_put_attr;
	}

	ks_xact_queue_request(xact, req);

	return req;

err_put_attr_status:
err_put_attr:
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return ks_req_get(&ks_nomem_request);
}

int ks_pipeline_create(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_pipeline_queue_create(pipeline, xact);

	ks_xact_submit(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_create_failed;
	}

	ks_req_put(req);
	ks_xact_put(xact);

	return 0;

err_create_failed:
	ks_xact_put(xact);
err_xact_alloc:
	ks_pipeline_del(pipeline);

	return err;
}

static int ks_pipeline_update_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline = req->response_data;

	ks_pipeline_update_from_nlmsg(pipeline, req->xact->conn, nlh);

	ks_pipeline_put(pipeline);

	return 0;
}

struct ks_req *ks_pipeline_queue_update(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		goto err_req_alloc;

	req->type = KS_NETLINK_PIPELINE_SET;
	req->flags = NLM_F_REQUEST;
	req->response_callback = ks_pipeline_update_handle_response;
	req->response_data = ks_pipeline_get(pipeline);

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb)
		goto err_skb_alloc;
	int err;

	err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_ID,
			&pipeline->id,
			sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr_id;

	err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_STATUS,
			&pipeline->status,
			sizeof(pipeline->status));
	if (err < 0)
		goto err_put_attr_status;

	ks_xact_queue_request(xact, req);

	return req;

err_put_attr_status:
err_put_attr_id:
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return ks_req_get(&ks_nomem_request);
}

int ks_pipeline_update(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_pipeline_queue_update(pipeline, xact);

	ks_xact_submit(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_update_failed;
	}

	ks_req_put(req);
	ks_xact_put(xact);

	return 0;

err_update_failed:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

int ks_pipeline_restart(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	ks_req_put(ks_xact_queue_new_request(xact, KS_NETLINK_BEGIN,
						NLM_F_REQUEST));

	struct ks_req *req1;
	pipeline->status = KS_PIPELINE_STATUS_OPEN;
	req1 = ks_pipeline_queue_update(pipeline, xact);

	struct ks_req *req2;
	pipeline->status = KS_PIPELINE_STATUS_FLOWING;
	req2 = ks_pipeline_queue_update(pipeline, xact);

	ks_req_put(ks_xact_queue_new_request(xact, KS_NETLINK_COMMIT,
						NLM_F_REQUEST));

	ks_xact_submit(xact);

	ks_req_wait(req1);
	if (req1->err < 0) {
		err = req1->err;
		ks_req_put(req1);

		goto err_update_failed_1;
	}
	ks_req_put(req1);

	ks_req_wait(req2);
	if (req2->err < 0) {
		err = req2->err;
		ks_req_put(req2);

		goto err_update_failed_2;
	}
	ks_req_put(req2);

	ks_xact_put(xact);

	return 0;

	ks_req_put(req1);
err_update_failed_1:
	ks_req_put(req2);
err_update_failed_2:
	ks_xact_abort(xact);
//err_xact_begin:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

static int ks_pipeline_destroy_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline = req->response_data;

	ks_pipeline_update_from_nlmsg(pipeline, req->xact->conn, nlh);

	ks_pipeline_put(pipeline);

	return 0;
}

struct ks_req *ks_pipeline_queue_destroy(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		goto err_req_alloc;

	req->type = KS_NETLINK_PIPELINE_DEL;
	req->flags = NLM_F_REQUEST;
	req->response_callback = ks_pipeline_destroy_handle_response;
	req->response_data = ks_pipeline_get(pipeline);

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb)
		goto err_skb_alloc;

	int err;

	err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_ID,
			&pipeline->id,
			sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr_id;

	ks_xact_queue_request(xact, req);

	return req;

err_put_attr_id:
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return ks_req_get(&ks_nomem_request);
}

int ks_pipeline_destroy(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_pipeline_queue_destroy(pipeline, xact);

	ks_xact_submit(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_destroy_failed;
	}

	ks_req_put(req);
	ks_xact_put(xact);

	return 0;

err_destroy_failed:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

int ks_pipeline_update_chans(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	err = ks_xact_begin(xact);
	if (err < 0)
		goto err_xact_begin;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {

		struct ks_chan *chan = pipeline->chans[i];

		if (conn->debug_netlink)
			ks_chan_dump(chan, conn, LOG_DEBUG);

		struct ks_req *req;
		req = ks_chan_queue_update(chan, xact);

		ks_req_wait(req);

		if (req->err < 0) {
			err = req->err;
			ks_req_put(req);

			goto err_update_failed;
		}

		ks_req_put(req);
	}

	err = ks_xact_commit(xact);
	if (err < 0)
		goto err_xact_commit;

	ks_xact_put(xact);

	return 0;

err_xact_commit:
err_update_failed:
	ks_xact_abort(xact);
err_xact_begin:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

int ks_pipeline_autoroute(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn,
	struct ks_node *src_node,
	struct ks_node *dst_node)
{
	int err;

	pthread_rwlock_wrlock(&conn->topology_lock);

	ks_router_run(src_node, dst_node);

	int nchans = 0;
	struct ks_node *node;
	for(node = dst_node; node->router_prev;
	    node = node->router_prev, nchans++);

	if (node != src_node) {
		err = -EHOSTUNREACH;
		goto err_no_path;
	}

	int i;
	for(node = dst_node, i=0; node->router_prev;
	    node = node->router_prev, i++) {
		pipeline->chans[pipeline->chans_cnt + nchans - i - 1] =
			ks_chan_get(node->router_prev_thru);
	}

	pipeline->chans_cnt += nchans;

	pthread_rwlock_unlock(&conn->topology_lock);

	return 0;

err_no_path:
	pthread_rwlock_unlock(&conn->topology_lock);

	return err;
}


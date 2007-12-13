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

static __u32 ks_pipeline_nlh_to_id(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_PIPELINEATTR_ID)
			return *(__u32 *)KS_ATTR_DATA(attr);
	}

	return 0;
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

			ks_chan_put(chan);
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

		pipeline = _ks_pipeline_get_by_id(conn,
				ks_pipeline_nlh_to_id(conn, nlh));
		if (pipeline) {
			ks_pipeline_put(pipeline);
			break;
		}

		pipeline = ks_pipeline_create_from_nlmsg(conn, nlh);
		if (!pipeline) {
			// FIXME
		}

		ks_pipeline_add(pipeline, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);

		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_DEL: {
		__u32 id = ks_pipeline_nlh_to_id(conn, nlh);
		struct ks_pipeline *pipeline = _ks_pipeline_get_by_id(conn, id);
		if (!pipeline) {
			report_conn(conn, LOG_ERR,
				"Pipeline ID %08x not found!\n", id);
			break;
		}

		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);
		ks_pipeline_del(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_SET: {
		__u32 id = ks_pipeline_nlh_to_id(conn, nlh);
		struct ks_pipeline *pipeline = _ks_pipeline_get_by_id(conn, id);
		if (!pipeline) {
			report_conn(conn, LOG_ERR,
				"Pipeline ID %08x not found!\n", id);
			break;
		}

		ks_pipeline_update_from_nlmsg(pipeline, conn, nlh);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, pipeline);

		ks_pipeline_put(pipeline);
	}
	break;
	}
}

void ks_pipeline_nlmsg_dump(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	const char *prefix)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_PIPELINEATTR_ID:
			report_conn(conn, LOG_DEBUG,
				"%s  ID    : 0x%08x\n",	prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_PIPELINEATTR_PATH:
			report_conn(conn, LOG_DEBUG,
				"%s  Path  : '%s'\n", prefix,
				strndupa(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr)));
		break;

		case KS_PIPELINEATTR_STATUS:
			report_conn(conn, LOG_DEBUG,
				"%s  Status: %d (%s)\n", prefix,
				*(__u32 *)KS_ATTR_DATA(attr),
				ks_pipeline_status_to_string(
					*(__u32 *)KS_ATTR_DATA(attr)));
		break;

		case KS_PIPELINEATTR_CHAN_ID:
			report_conn(conn, LOG_DEBUG,
				"%s  Channel: 0x%08x\n", prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default:
			report_conn(conn, LOG_ERR,
				"%s  Attribute '%s'\n", prefix,
				ks_netlink_pipeline_attr_to_string(
					attr->type));
		}
	}
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


int ks_pipeline_create(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_PIPELINE_NEW;
	req->flags = NLM_F_REQUEST;

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

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
			goto err_put_attr_chan_id;
	}

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_request_failed;
	}

	ks_pipeline_update_from_nlmsg(pipeline, conn, req->response_payload);

	pthread_rwlock_wrlock(&conn->topology_lock);
	ks_pipeline_add(pipeline, conn);
	pthread_rwlock_unlock(&conn->topology_lock);

	ks_req_put(req);

	return 0;

	ks_pipeline_del(pipeline);
err_request_failed:
err_put_attr_chan_id:
err_put_attr_status:
	/* skb is freed in req_put */
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return err;
}

int ks_pipeline_update(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_PIPELINE_SET;
	req->flags = NLM_F_REQUEST;

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

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

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_request_failed;
	}

	ks_req_put(req);

	return 0;

err_request_failed:
err_put_attr_status:
err_put_attr_id:
	/* skb is freed in req_put */
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return err;
}

int ks_pipeline_restart(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	pipeline->status = KS_PIPELINE_STATUS_OPEN;
	ks_pipeline_update(pipeline, conn);

	pipeline->status = KS_PIPELINE_STATUS_FLOWING;
	ks_pipeline_update(pipeline, conn);

	return 0;
}

int ks_pipeline_destroy(struct ks_pipeline *pipeline, struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_PIPELINE_DEL;
	req->flags = NLM_F_REQUEST;

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	err = ks_netlink_put_attr(req->skb, KS_PIPELINEATTR_ID,
			&pipeline->id,
			sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr_id;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		goto err_request_failed;
	}

	ks_req_put(req);

	return 0;

err_request_failed:
err_put_attr_id:
	/* skb is freed in req_put */
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return err;
}

int ks_pipeline_update_chans(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn)
{
	int err;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {

		struct ks_chan *chan = pipeline->chans[i];

		err = ks_chan_update(chan, conn);
		if (err < 0)
			goto err_update_failed;
	}

	return 0;

err_update_failed:

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

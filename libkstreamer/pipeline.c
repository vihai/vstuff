/*
 * Userland Kstreamer Helper Routines
 *
 * Copyright (C) 2006 Daniele Orlandi
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

#include "pipeline.h"
#include "netlink.h"
#include "channel.h"
#include "node.h"
#include "dynattr.h"
#include "req.h"
#include "xact.h"
#include "router.h"
#include "logging.h"

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

	pipeline->chans_cnt = 0;

	ks_pipeline_put(pipeline);
}

struct ks_pipeline *ks_pipeline_get_by_id(
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

struct ks_pipeline *ks_pipeline_get_by_path(
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

struct ks_pipeline *ks_pipeline_get_by_string(
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

		pipeline = ks_pipeline_get_by_path(conn, real_path + 4);
		free(real_path);
	} else
		pipeline = ks_pipeline_get_by_id(conn, atoi(pipeline_str));

	return pipeline;
}

struct ks_pipeline *ks_pipeline_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_PIPELINEATTR_ID)
			return ks_pipeline_get_by_id(conn,
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

	if (pipeline)
		pipeline->refcnt++;

	return pipeline;
}

void ks_pipeline_put(struct ks_pipeline *pipeline)
{
	assert(pipeline->refcnt > 0);

	pipeline->refcnt--;

	if (!pipeline->refcnt) {
		if (pipeline->path)
			free(pipeline->path);

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
	struct ks_conn *conn)
{
	report_conn(conn, LOG_DEBUG, "  ID    : 0x%08x\n", pipeline->id);
	report_conn(conn, LOG_DEBUG, "  Path  : '%s'\n", pipeline->path);
	report_conn(conn, LOG_DEBUG, "  Status: %s\n",
		ks_pipeline_status_to_string(pipeline->status));

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {

		struct ks_chan *chan = pipeline->chans[i];

		report_conn(conn, LOG_DEBUG, "%s =>(%s)=> %s\n",
			chan->from->path,
			chan->path,
			chan->to->path);

		struct ks_dynattr_instance *dynattr;
		list_for_each_entry(dynattr, &chan->dynattrs, node) {
			report_conn(conn, LOG_DEBUG,
				"  DynAttr: %s\n", dynattr->dynattr->name);
		}
	}
}

#if 0
int ks_pipeline_descr_do_autoroute(
	struct ks_pipeline_descr *pd,
	struct ks_conn *conn,
	int pos)
{
	int err;

	pthread_mutex_lock(&conn->topology_lock);

	struct ks_node *src_node;
	struct ks_node *dst_node;

	if (pos == 0)
		src_node = pd->ep1;
	else
		src_node = pd->chans[pos-1]->from;

	if (pos == pd->nchans-1)
		dst_node = pd->ep2;
	else
		dst_node = pd->chans[pos]->to;

	ks_router_run(src_node, dst_node);

	int path_len = 0;
	struct ks_node *node;
	for(node = dst_node; node->router_prev;
	    node = node->router_prev, path_len++);

	if (node != src_node) {
		err = -EHOSTUNREACH;
		goto err_no_path;
	}

	/* Make space in the array for path_len-1 chans ad one slot is
	 * altready free, as it contains NULL
	 */

	int i;
	for(i=pd->nchans-1; i>pos; i--) {
		pd->chans[i] = pd->chans[i-path_len-1];
		pd->pars[i] = pd->pars[i-path_len-1];
	}

	pd->nchans += path_len-1;

	for(node = dst_node, i=pos+path_len-1; node->router_prev;
	    node = node->router_prev, i--) {
		pd->chans[i] = node->router_prev_thru;
		pd->pars[i] = pd->pars[pos];
	}

	pthread_mutex_unlock(&conn->topology_lock);

	return 0;

err_no_path:
	pthread_mutex_unlock(&conn->topology_lock);

	return err;
}

int ks_pipeline_descr_autoroute(
	struct ks_pipeline_descr *pd,
	struct ks_conn *conn)
{
	int i;
	for (i=0; i<pd->nchans; i++) {

		if (pd->chans[i] == NULL) {
			ks_pipeline_descr_do_autoroute(pd, conn, i);
			break;
		}
	}

	return 0;
}
#endif

struct ks_pipeline *ks_pipeline_create_from_nlmsg(
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
			if (pipeline->path)
				free(pipeline->path);

			pipeline->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_PIPELINEATTR_CHAN_ID: {
			struct ks_chan *chan;
			chan = ks_chan_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
			if (!chan) {
				report_conn(conn, LOG_ERR,
					"Channel 0x%08x not found!\n",
					*(__u32 *)KS_ATTR_DATA(attr));
				break;
			}

			pipeline->chans[pipeline->chans_cnt] = chan;
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

void ks_pipeline_update_from_nlmsg(
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

static int ks_pipeline_create_payload_fill(
	struct ks_req *req,
	struct sk_buff *skb,
	void *data)
{
	struct ks_pipeline *pipeline = data;
	int err;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_STATUS,
			&pipeline->status,
			sizeof(pipeline->status));
	if (err < 0)
		goto err_put_attr_status;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_CHAN_ID,
				&pipeline->chans[i]->id,
				sizeof(pipeline->chans[i]->id));
		if (err < 0)
			goto err_put_attr;
	}

	return 0;

err_put_attr_status:
err_put_attr:

	return err;
}

static int ks_pipeline_create_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_pipeline *pipeline = data;

	if (nlh->nlmsg_type != KS_NETLINK_PIPELINE_NEW)
		return 0;

	ks_pipeline_update_from_nlmsg(pipeline, req->xact->conn, nlh);

	return 0;
}

struct ks_req *ks_pipeline_queue_create(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = KS_NETLINK_PIPELINE_NEW;
	req->flags = NLM_F_REQUEST;
	req->data = pipeline;
	req->response_callback = ks_pipeline_create_handle_response;
	req->request_fill_callback = ks_pipeline_create_payload_fill;

	ks_xact_queue_request(xact, req);

	return req;
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

	return 0;

err_create_failed:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

static int ks_pipeline_update_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_pipeline *pipeline = data;

	if (nlh->nlmsg_type != KS_NETLINK_PIPELINE_SET)
		return 0;

	ks_pipeline_update_from_nlmsg(pipeline, req->xact->conn, nlh);

	return 0;
}

static int ks_pipeline_update_payload_fill(
	struct ks_req *req,
	struct sk_buff *skb,
	void *data)
{
	struct ks_pipeline *pipeline = data;
	int err;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_ID,
			&pipeline->id,
			sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr_id;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_STATUS,
			&pipeline->status,
			sizeof(pipeline->status));
	if (err < 0)
		goto err_put_attr_status;

	return 0;

err_put_attr_status:
err_put_attr_id:

	return err;
}

struct ks_req *ks_pipeline_queue_update(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = KS_NETLINK_PIPELINE_SET;
	req->flags = NLM_F_REQUEST;
	req->data = pipeline;
	req->response_callback = ks_pipeline_update_handle_response;
	req->request_fill_callback = ks_pipeline_update_payload_fill;

	ks_xact_queue_request(xact, req);

	return req;
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

	return 0;

err_update_failed:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

static int ks_pipeline_destroy_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_pipeline *pipeline = data;

	if (nlh->nlmsg_type != KS_NETLINK_PIPELINE_DEL)
		return 0;

	ks_pipeline_update_from_nlmsg(pipeline, req->xact->conn, nlh);

	return 0;
}

static int ks_pipeline_destroy_payload_fill(
	struct ks_req *req,
	struct sk_buff *skb,
	void *data)
{
	struct ks_pipeline *pipeline = data;
	int err;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_ID,
			&pipeline->id,
			sizeof(pipeline->id));
	if (err < 0)
		goto err_put_attr_id;

	return 0;

err_put_attr_id:

	return err;
}

struct ks_req *ks_pipeline_queue_destroy(
	struct ks_pipeline *pipeline,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = KS_NETLINK_PIPELINE_DEL;
	req->flags = NLM_F_REQUEST;
	req->data = pipeline;
	req->response_callback = ks_pipeline_destroy_handle_response;
	req->request_fill_callback = ks_pipeline_destroy_payload_fill;

	ks_xact_queue_request(xact, req);

	return req;
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

		ks_chan_dump(chan, conn);

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

	return 0;

err_xact_commit:
err_update_failed:
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

	pthread_mutex_lock(&conn->topology_lock);

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
			node->router_prev_thru;
	}

	pipeline->chans_cnt += nchans;

	pthread_mutex_unlock(&conn->topology_lock);

	return 0;

err_no_path:
	pthread_mutex_unlock(&conn->topology_lock);

	return err;
}


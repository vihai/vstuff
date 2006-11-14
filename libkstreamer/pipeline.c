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

#define _GNU_SOURCE
#define _LIBKSTREAMER_PRIVATE_
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

#define PIPELINE_HASHBITS 8
#define PIPELINE_HASHSIZE ((1 << PIPELINE_HASHBITS) - 1)

static struct hlist_head ks_pipelines_hash[PIPELINE_HASHSIZE];

static inline struct hlist_head *ks_pipeline_get_hash(int id)
{
	return &ks_pipelines_hash[id & (PIPELINE_HASHSIZE - 1)];
}

void ks_pipeline_add(struct ks_pipeline *pipeline)
{
	hlist_add_head(
		&ks_pipeline_get(pipeline)->node,
		ks_pipeline_get_hash(pipeline->id));
}

void ks_pipeline_del(struct ks_pipeline *pipeline)
{
	hlist_del(&pipeline->node);

	ks_pipeline_put(pipeline);
}

struct ks_pipeline *ks_pipeline_get_by_id(int id)
{
	struct ks_pipeline *pipeline;
	struct hlist_node *t;

	hlist_for_each_entry(pipeline, t, ks_pipeline_get_hash(id), node) {
		if (pipeline->id == id)
			return ks_pipeline_get(pipeline);
	}

	return NULL;
}

struct ks_pipeline *ks_pipeline_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_PIPELINEATTR_ID)
			return ks_pipeline_get_by_id(
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
		return "Link ID";
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

void ks_pipeline_dump(struct ks_pipeline *pipeline)
{
	printf("  ID    : 0x%08x\n", pipeline->id);
	printf("  Path  : '%s'\n", pipeline->path);
	printf("  Status: %s\n",
		ks_pipeline_status_to_string(pipeline->status));

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {

		struct ks_chan *chan = pipeline->chans[i];

		printf("%s =>(%s)=> %s\n",
			chan->from->path,
			chan->path,
			chan->to->path);

		struct ks_dynattr_instance *dynattr;
		list_for_each_entry(dynattr, &chan->dynattrs, node) {
			printf("  DynAttr: %s\n", dynattr->dynattr->name);
		}
	}
}

struct ks_pipeline *ks_pipeline_create_from_nlmsg(struct nlmsghdr *nlh)
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

		default:
			printf("   Attribute '%s'\n",
				ks_netlink_pipeline_attr_to_string(
					attr->type));
		}
	}

	return pipeline;
}

void ks_pipeline_update_from_nlmsg(
	struct ks_pipeline *pipeline,
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

		default:
			printf("   Attribute '%s'\n",
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

	ks_pipeline_update_from_nlmsg(pipeline, nlh);

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

	err = ks_xact_begin(xact);
	if (err < 0)
		goto err_xact_begin;

	struct ks_req *req;
	req = ks_pipeline_queue_create(pipeline, xact);

	ks_xact_run(xact);
	ks_req_waitloop(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		ks_xact_abort(xact);

		goto err_create_failed;
	}

	ks_req_put(req);

	err = ks_xact_commit(xact);
	if (err < 0)
		goto err_xact_commit;

	return 0;

err_xact_commit:
err_create_failed:
err_xact_begin:
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

	ks_pipeline_update_from_nlmsg(pipeline, nlh);

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

	err = ks_xact_begin(xact);
	if (err < 0)
		goto err_xact_begin;

	struct ks_req *req;
	req = ks_pipeline_queue_update(pipeline, xact);

	ks_xact_run(xact);
	ks_req_waitloop(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		ks_xact_abort(xact);

		goto err_update_failed;
	}

	ks_req_put(req);

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

static int ks_pipeline_destroy_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_pipeline *pipeline = data;

	if (nlh->nlmsg_type != KS_NETLINK_PIPELINE_DEL)
		return 0;

	ks_pipeline_update_from_nlmsg(pipeline, nlh);

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

	err = ks_xact_begin(xact);
	if (err < 0)
		goto err_xact_begin;

	struct ks_req *req;
	req = ks_pipeline_queue_destroy(pipeline, xact);

	ks_xact_run(xact);
	ks_req_waitloop(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		ks_xact_abort(xact);

		goto err_destroy_failed;
	}

	ks_req_put(req);

	err = ks_xact_commit(xact);
	if (err < 0)
		goto err_xact_commit;

	return 0;

err_xact_commit:
err_destroy_failed:
err_xact_begin:
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

		struct ks_req *req;
		req = ks_chan_queue_update(chan, xact);

		ks_xact_run(xact);
		ks_req_waitloop(req);

		if (req->err < 0) {
			err = req->err;
			ks_req_put(req);

			ks_xact_abort(xact);

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

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
#include "link.h"
#include "request.h"

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
	case KS_PIPELINEATTR_LINK_ID:
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
}

int ks_pipeline_write_to_nlmsg(
	struct ks_pipeline *pipeline,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	__u32 pid, __u32 seq, __u16 flags)
{
	int err = -ENOBUFS;

	void *oldtail = skb->tail;

	struct nlmsghdr *nlh;
	nlh = ks_nlmsg_put(skb, pid, seq, message_type, flags, 0);
	if (!nlh)
		goto err_nlmsg_put;

	err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_STATUS,
			&pipeline->status,
			sizeof(pipeline->status));
	if (err < 0)
		goto err_put_attr;

	int i;
	for(i=0; i<pipeline->links_cnt; i++) {
		err = ks_netlink_put_attr(skb, KS_PIPELINEATTR_LINK_ID,
				&pipeline->links[i]->id,
				sizeof(pipeline->links[i]->id));
		if (err < 0)
			goto err_put_attr;
	}

	nlh->nlmsg_len = skb->tail - oldtail;

	return 0;

err_put_attr:
err_nlmsg_put:
	skb_trim(skb, oldtail - skb->data);

	return err;
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
		case KS_PIPELINEATTR_PATH:
		break;

		case KS_PIPELINEATTR_STATUS:
			pipeline->status =
			        *(__u32 *)KS_ATTR_DATA(attr);
		break;

		default:
			printf("   Attribute '%s'\n",
				ks_netlink_pipeline_attr_to_string(
					attr->type));
		}
	}
}

int ks_pipeline_create_async(
	struct ks_conn *conn,
	struct ks_pipeline *pipeline)
{
	struct sk_buff *skb;
	int err;

	skb = skb_alloc(4096, 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_request *req;
	req = ks_request_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_BEGIN,
			NLM_F_REQUEST, 0);

	err = ks_pipeline_write_to_nlmsg(pipeline, skb, KS_NETLINK_PIPELINE_NEW,
			getpid(), req->seqnum, NLM_F_REQUEST | NLM_F_ACK);
	if (err < 0)
		goto err_fill_msg;

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_COMMIT,
			NLM_F_REQUEST, 0);

	ks_conn_add_request(conn, req);
	ks_request_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

err_fill_msg:
	ks_request_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
}

int ks_pipeline_create(
	struct ks_conn *conn,
	struct ks_pipeline *pipeline)
{
	ks_pipeline_create_async(conn, pipeline);

	ks_netlink_waitloop(conn);

	return 0;
}

int ks_pipeline_update(struct ks_conn *conn, struct ks_pipeline *pipeline)
{
	struct sk_buff *skb;
	int err;

	skb = skb_alloc(4096, 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_request *req;
	req = ks_request_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	err = ks_pipeline_write_to_nlmsg(pipeline, skb, KS_NETLINK_PIPELINE_SET,
			getpid(), req->seqnum, NLM_F_REQUEST);
	if (err < 0)
		goto err_fill_msg;

	ks_conn_add_request(conn, req);
	ks_request_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

err_fill_msg:
	ks_request_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
}

int ks_pipeline_update_links(struct ks_conn *conn, struct ks_pipeline *pipeline)
{
	struct sk_buff *skb;
	int err;

	skb = skb_alloc(4096, 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_request *req;
	req = ks_request_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	int i;
	for(i=0; i<pipeline->links_cnt; i++) {

		struct ks_link *link = pipeline->links[i];

		err = ks_link_write_to_nlmsg(link, skb, KS_NETLINK_LINK_SET,
				getpid(), req->seqnum,
				NLM_F_REQUEST | NLM_F_MULTI);
		if (err < 0)
			goto err_fill_msg;
	}

	ks_nlmsg_put(skb, getpid(), req->seqnum, NLMSG_DONE,
			NLM_F_REQUEST | NLM_F_MULTI, 0);

	ks_conn_add_request(conn, req);
	ks_request_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

err_fill_msg:
	ks_request_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
}

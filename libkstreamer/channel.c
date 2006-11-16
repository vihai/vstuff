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
#include <errno.h>
#include <assert.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include "channel.h"
#include "node.h"
#include "dynattr.h"
#include "netlink.h"
#include "req.h"
#include "xact.h"

struct hlist_head ks_chans_hash[CHAN_HASHSIZE];

static inline struct hlist_head *ks_chan_get_hash(int id)
{
	return &ks_chans_hash[id & (CHAN_HASHSIZE - 1)];
}

const char *ks_netlink_chan_attr_to_string(
		enum ks_chan_attribute_type type)
{
	switch(type) {
	case KS_CHANATTR_ID:
		return "ID";
	case KS_CHANATTR_PATH:
		return "PATH";
	case KS_CHANATTR_FROM:
		return "FROM";
	case KS_CHANATTR_TO:
		return "TO";
	}

	return "UNKNOWN";
}

void ks_chan_add(struct ks_chan *chan)
{
	hlist_add_head(
		&ks_chan_get(chan)->node,
		ks_chan_get_hash(chan->id));
}

void ks_chan_del(struct ks_chan *chan)
{
	hlist_del(&chan->node);

	ks_chan_put(chan);
}

struct ks_chan *ks_chan_get_by_id(int id)
{
	struct ks_chan *chan;
	struct hlist_node *t;

	hlist_for_each_entry(chan, t, ks_chan_get_hash(id), node) {
		if (chan->id == id)
			return ks_chan_get(chan);
	}

	return NULL;
}

struct ks_chan *ks_chan_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_CHANATTR_ID)
			return ks_chan_get_by_id(*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

struct ks_chan *ks_chan_alloc(void)
{
	struct ks_chan *chan;

	chan = malloc(sizeof(*chan));
	if (!chan)
		return NULL;

	memset(chan, 0, sizeof(*chan));

	chan->refcnt = 1;

	INIT_LIST_HEAD(&chan->dynattrs);

	return chan;
}

struct ks_chan *ks_chan_get(struct ks_chan *chan)
{
	assert(chan->refcnt > 0);

	if (chan)
		chan->refcnt++;

	return chan;
}

void ks_chan_put(struct ks_chan *chan)
{
	assert(chan->refcnt > 0);

	chan->refcnt--;

	if (!chan->refcnt) {
		struct ks_dynattr_instance *dynattr, *t;

		list_for_each_entry_safe(dynattr, t, &chan->dynattrs, node) {
			list_del(&dynattr->node);
			free(dynattr);
		}

		free(chan);
	}
}

struct ks_chan *ks_chan_create_from_nlmsg(struct nlmsghdr *nlh)
{
	struct ks_chan *chan;

	chan = ks_chan_alloc();
	if (!chan)
		return NULL;

	ks_chan_update_from_nlmsg(chan, nlh);

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_CHANATTR_ID:
			chan->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_CHANATTR_PATH:
			if (chan->path)
				free(chan->path);

			chan->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_CHANATTR_FROM:
			chan->from = ks_node_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_CHANATTR_TO:
			chan->to = ks_node_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default: {
			struct ks_dynattr *dynattr_class;
			dynattr_class = ks_dynattr_get_by_id(attr->type);
			if (!dynattr_class) {
				fprintf(stderr, "   Attribute %d unknown\n", attr->type);
				break;
			}

			struct ks_dynattr_instance *dynattr;
			dynattr = malloc(sizeof(*dynattr) + attr->len);
			if (!dynattr) {
				// FIXME
				break;
			}

			dynattr->dynattr = dynattr_class;
			dynattr->len = KS_ATTR_PAYLOAD(attr);
			memcpy(dynattr->payload,
				KS_ATTR_DATA(attr),
				KS_ATTR_PAYLOAD(attr));

			list_add(&dynattr->node, &chan->dynattrs);
		}
		}
	}

	return chan;
}

void ks_chan_update_from_nlmsg(struct ks_chan *chan, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_CHANATTR_ID:
		case KS_CHANATTR_PATH:
		case KS_CHANATTR_FROM:
		case KS_CHANATTR_TO:
			/* Are updates to these allowed? */
		break;

		default: {
			struct ks_dynattr_instance *dynattr;
			list_for_each_entry(dynattr, &chan->dynattrs, node) {
				if (dynattr->dynattr->id == attr->type)
					assert(dynattr->len ==
						KS_ATTR_PAYLOAD(attr));

					memcpy(dynattr->payload,
						KS_ATTR_DATA(attr),
						KS_ATTR_PAYLOAD(attr));

					break;
				}
		}
		}
	}
}

void ks_chan_dump(struct ks_chan *chan)
{
	fprintf(stderr, "  ID    : 0x%08x\n", chan->id);
	fprintf(stderr, "  Path  : '%s'\n", chan->path);

	if (chan->from)
		fprintf(stderr, "  From  : 0x%08x (%s)\n",
			chan->from->id, chan->from->path);

	if (chan->to)
		fprintf(stderr, "  To    : 0x%08x (%s)\n",
			chan->to->id, chan->to->path);

	struct ks_dynattr_instance *dynattr;
	list_for_each_entry(dynattr, &chan->dynattrs, node) {
		fprintf(stderr, "  Dynattr: %s\n", dynattr->dynattr->name);

		fprintf(stderr, "  Data: ");

		int i;
		for(i=0; i<dynattr->len; i++) {
			fprintf(stderr, "%02x ",
				*(dynattr->payload + i));
		}

		fprintf(stderr, "\n");
	}
}

static int ks_chan_update_handle_response(
	struct ks_req *req,
	struct nlmsghdr *nlh,
	void *data)
{
	struct ks_chan *chan = data;

	if (nlh->nlmsg_type != KS_NETLINK_PIPELINE_SET)
		return 0;

	ks_chan_update_from_nlmsg(chan, nlh);

	return 0;
}

static int ks_chan_update_payload_fill(
	struct ks_req *req,
	struct sk_buff *skb,
	void *data)
{
	struct ks_chan *chan = data;
	int err;

	err = ks_netlink_put_attr(skb, KS_CHANATTR_ID,
			&chan->id,
			sizeof(chan->id));
	if (err < 0)
		goto err_put_attr_id;

	struct ks_dynattr_instance *dynattr;
	list_for_each_entry(dynattr, &chan->dynattrs, node) {

		err = ks_netlink_put_attr(skb, dynattr->dynattr->id,
				dynattr->payload, dynattr->len);
		if (err < 0)
			goto err_put_attr;
	}

	return 0;

err_put_attr:
err_put_attr_id:

	return err;
}

struct ks_req *ks_chan_queue_update(
	struct ks_chan *chan,
	struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = KS_NETLINK_CHAN_SET;
	req->flags = NLM_F_REQUEST;
	req->data = chan;
	req->response_callback = ks_chan_update_handle_response;
	req->request_fill_callback = ks_chan_update_payload_fill;

	ks_xact_queue_request(xact, req);

	return req;
}

int ks_chan_update(struct ks_chan *chan, struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_chan_queue_update(chan, xact);

	ks_xact_run(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);

		ks_xact_abort(xact);

		goto err_update_failed;
	}

	ks_req_put(req);

	return 0;

err_update_failed:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

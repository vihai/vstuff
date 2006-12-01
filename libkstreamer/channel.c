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
#include "logging.h"

static inline struct hlist_head *ks_chan_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->chans_hash[id & (DYNATTR_HASHSIZE - 1)];
}

void ks_chan_add(struct ks_chan *chan, struct ks_conn *conn)
{
	chan->conn = conn;

	hlist_add_head(
		&ks_chan_get(chan)->node,
		ks_chan_get_hash(conn, chan->id));
}

void ks_chan_del(struct ks_chan *chan)
{
	hlist_del(&chan->node);

	ks_chan_put(chan);
}

struct ks_chan *ks_chan_get_by_id(struct ks_conn *conn, int id)
{
	struct ks_chan *chan;
	struct hlist_node *t;

	hlist_for_each_entry(chan, t, ks_chan_get_hash(conn, id), node) {
		if (chan->id == id)
			return ks_chan_get(chan);
	}

	return NULL;
}

struct ks_chan *ks_chan_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	struct ks_chan *chan;
	struct hlist_node *t;

	int i;
	for(i=0; i<ARRAY_SIZE(conn->chans_hash); i++) {
		hlist_for_each_entry(chan, t, &conn->chans_hash[i], node) {
			if (!strcmp(chan->path, path))
				return ks_chan_get(chan);
		}
	}

	return NULL;
}

struct ks_chan *ks_chan_get_by_token(
	struct ks_conn *conn,
	struct ks_pd_token *token)
{
	struct ks_chan *chan;

	switch(token->id) {
	case TK_STRING: {
		char *real_path;
		real_path = realpath(token->text, NULL);
		if (!real_path) {
			report_conn(conn, LOG_WARNING,
				"Cannot resolve path '%s': %s\n",
				token->text, strerror(errno));
			return NULL;
		}

		chan = ks_chan_get_by_path(conn, real_path + strlen("/sys"));
		free(real_path);
	}
	break;

	case TK_INTEGER:
		chan = ks_chan_get_by_id(conn, atoi(token->text));
	break;

	case TK_HEXINT:
		printf("NOT IMPLEMENTED!\n");
		chan = NULL;
	break;

	default:
		assert(0);
		chan = NULL;
	}

	return chan;
}

struct ks_chan *ks_chan_get_by_nlid(struct ks_conn *conn, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_CHANATTR_ID)
			return ks_chan_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

#if 0
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
#endif

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

struct ks_chan *ks_chan_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_chan *chan;

	chan = ks_chan_alloc();
	if (!chan)
		return NULL;

	ks_chan_update_from_nlmsg(chan, conn, nlh);

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
			chan->from = ks_node_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_CHANATTR_TO:
			chan->to = ks_node_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default: {
			struct ks_dynattr *dynattr_class;
			dynattr_class = ks_dynattr_get_by_id(conn, attr->type);
			if (!dynattr_class) {
				report_conn(conn, LOG_WARNING,
					"Attribute %d unknown\n", attr->type);
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

void ks_chan_update_from_nlmsg(
	struct ks_chan *chan,
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
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

void ks_chan_dump(
	struct ks_chan *chan,
	struct ks_conn *conn)
{
	report_conn(conn, LOG_DEBUG, "  ID    : 0x%08x\n", chan->id);
	report_conn(conn, LOG_DEBUG, "  Path  : '%s'\n", chan->path);

	if (chan->from)
		report_conn(conn, LOG_DEBUG, "  From  : 0x%08x (%s)\n",
			chan->from->id, chan->from->path);

	if (chan->to)
		report_conn(conn, LOG_DEBUG, "  To    : 0x%08x (%s)\n",
			chan->to->id, chan->to->path);

	struct ks_dynattr_instance *dynattr;
	list_for_each_entry(dynattr, &chan->dynattrs, node) {
		report_conn(conn, LOG_DEBUG, "  Dynattr: %s\n", dynattr->dynattr->name);

		report_conn(conn, LOG_DEBUG, "  Data: ");

		int i;
		for(i=0; i<dynattr->len; i++) {
			report_conn(conn, LOG_DEBUG, "%02x ",
				*(dynattr->payload + i));
		}

		report_conn(conn, LOG_DEBUG, "\n");
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

	ks_chan_update_from_nlmsg(chan, req->xact->conn, nlh);

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

	ks_xact_submit(xact);
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

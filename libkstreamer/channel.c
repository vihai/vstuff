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
#include <assert.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/node.h>
#include <libkstreamer/feature.h>
#include <libkstreamer/req.h>
#include <libkstreamer/logging.h>

static inline struct hlist_head *ks_chan_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->chans_hash[id & (FEATURE_HASHSIZE - 1)];
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

void ks_chan_flush(struct ks_conn *conn)
{
	struct hlist_node *pos, *n;
	struct ks_chan *chan;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->chans_hash); i++) {
		hlist_for_each_entry_safe(chan, pos, n,
					&conn->chans_hash[i], node) {

			hlist_del(&chan->node);
			ks_chan_put(chan);
		}
	}
}

struct ks_chan *_ks_chan_get_by_id(struct ks_conn *conn, int id)
{
	struct ks_chan *chan;
	struct hlist_node *t;

	hlist_for_each_entry(chan, t, ks_chan_get_hash(conn, id), node) {
		if (chan->id == id)
			return ks_chan_get(chan);
	}

	return NULL;
}

struct ks_chan *ks_chan_get_by_id(struct ks_conn *conn, int id)
{
	struct ks_chan *chan;

	pthread_rwlock_rdlock(&conn->topology_lock);
	chan = _ks_chan_get_by_id(conn, id);
	pthread_rwlock_unlock(&conn->topology_lock);

	return chan;
}

static struct ks_chan *_ks_chan_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	char *real_path;
	real_path = realpath(path, NULL);
	if (!real_path) {
		report_conn(conn, LOG_WARNING,
			"Cannot resolve path '%s': %s\n",
			path, strerror(errno));
		return NULL;
	}

	char *sys_path = real_path + strlen("/sys");

	struct ks_chan *chan;
	struct hlist_node *t;

	int i;
	for(i=0; i<ARRAY_SIZE(conn->chans_hash); i++) {
		hlist_for_each_entry(chan, t, &conn->chans_hash[i], node) {
			if (!strcmp(chan->path, sys_path)) {
				free(real_path);
				return ks_chan_get(chan);
			}
		}
	}

	free(real_path);

	return NULL;
}

struct ks_chan *ks_chan_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	struct ks_chan *chan;

	pthread_rwlock_rdlock(&conn->topology_lock);
	chan = _ks_chan_get_by_path(conn, path);
	pthread_rwlock_unlock(&conn->topology_lock);

	return chan;
}

static struct ks_chan *_ks_chan_get_by_token(
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

		chan = _ks_chan_get_by_path(conn, real_path + strlen("/sys"));
		free(real_path);
	}
	break;

	case TK_INTEGER:
		chan = _ks_chan_get_by_id(conn, atoi(token->text));
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

struct ks_chan *ks_chan_get_by_token(
	struct ks_conn *conn,
	struct ks_pd_token *token)
{
	struct ks_chan *chan;

	pthread_rwlock_rdlock(&conn->topology_lock);
	chan = _ks_chan_get_by_token(conn, token);
	pthread_rwlock_unlock(&conn->topology_lock);

	return chan;
}

static __u32 ks_chan_nlh_to_id(
	struct ks_conn *conn, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_CHANATTR_ID)
			return *(__u32 *)KS_ATTR_DATA(attr);
	}

	return 0;
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
	chan->cost = 1000;

	INIT_LIST_HEAD(&chan->features);

	return chan;
}

struct ks_chan *ks_chan_get(struct ks_chan *chan)
{
	assert(chan->refcnt > 0);

	if (chan) {
		pthread_mutex_lock(&refcnt_lock);
		chan->refcnt++;
		pthread_mutex_unlock(&refcnt_lock);
	}

	return chan;
}

void ks_chan_put(struct ks_chan *chan)
{
	assert(chan->refcnt > 0);

	pthread_mutex_lock(&refcnt_lock);
	int refcnt = --chan->refcnt;
	pthread_mutex_unlock(&refcnt_lock);

	if (!refcnt) {
		struct ks_feature_value *featval, *t;

		list_for_each_entry_safe(featval, t, &chan->features, node) {
			ks_feature_put(featval->feature);
			list_del(&featval->node);
			free(featval);
		}

		if (chan->path)
			free(chan->path);

		if (chan->from)
			ks_node_put(chan->from);

		if (chan->to)
			ks_node_put(chan->to);

		free(chan);
	}
}

static void ks_chan_update_from_nlmsg(
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
			struct ks_feature_value *featval;
			list_for_each_entry(featval, &chan->features, node) {
				if (featval->feature->id == attr->type)
					assert(featval->len ==
						KS_ATTR_PAYLOAD(attr));

					memcpy(featval->payload,
						KS_ATTR_DATA(attr),
						KS_ATTR_PAYLOAD(attr));

					break;
				}
		}
		}
	}
}

static struct ks_chan *ks_chan_create_from_nlmsg(
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
			assert(!chan->path);

			chan->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_CHANATTR_FROM:
			assert(!chan->from);

			chan->from = _ks_node_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_CHANATTR_TO:
			assert(!chan->to);

			chan->to = _ks_node_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default: {
			struct ks_feature *feature;
			feature = _ks_feature_get_by_id(conn, attr->type);
			if (!feature) {
				report_conn(conn, LOG_WARNING,
					"Attribute %d unknown\n", attr->type);
				break;
			}

			struct ks_feature_value *featval;
			featval = malloc(sizeof(*featval) + attr->len);
			if (!featval) {
				// FIXME
				ks_feature_put(feature);
				break;
			}

			featval->feature = ks_feature_get(feature);
			featval->len = KS_ATTR_PAYLOAD(attr);
			memcpy(featval->payload,
				KS_ATTR_DATA(attr),
				KS_ATTR_PAYLOAD(attr));

			list_add(&featval->node, &chan->features);

			ks_feature_put(feature);
		}
		}
	}

	return chan;
}

void ks_chan_handle_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	switch(nlh->nlmsg_type) {
	case KS_NETLINK_CHAN_NEW: {
		struct ks_chan *chan;

		chan = ks_chan_create_from_nlmsg(conn, nlh);
		if (!chan) {
			// FIXME
		}

		ks_chan_add(chan, conn); // CHECK FOR DUPEs FIXME TODO
		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_DEL: {
		__u32 id = ks_chan_nlh_to_id(conn, nlh);
		struct ks_chan *chan = _ks_chan_get_by_id(conn, id);
		if (!chan) {
			report_conn(conn, LOG_ERR,
				"Channel ID %08x not found!\n", id);
			break;
		}

		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);
		ks_chan_del(chan);
		ks_chan_put(chan);
	}
	break;

	case KS_NETLINK_CHAN_SET: {
		__u32 id = ks_chan_nlh_to_id(conn, nlh);
		struct ks_chan *chan = _ks_chan_get_by_id(conn, id);
		if (!chan) {
			report_conn(conn, LOG_ERR,
				"Channel ID %08x not found!\n", id);
			break;
		}

		ks_chan_update_from_nlmsg(chan, conn, nlh);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, chan);

		ks_chan_put(chan);
	}
	break;
	}
}

void ks_chan_nlmsg_dump(
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
		case KS_CHANATTR_ID:
			report_conn(conn, LOG_DEBUG,
				"%s  ID    : 0x%08x\n",	prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_CHANATTR_PATH:
			report_conn(conn, LOG_DEBUG,
				"%s  Path  : '%s'\n", prefix,
				strndupa(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr)));
		break;

		case KS_CHANATTR_FROM:
			report_conn(conn, LOG_DEBUG,
				"%s  From  : 0x%08x\n", prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_CHANATTR_TO:
			report_conn(conn, LOG_DEBUG,
				"%s  To    : 0x%08x\n", prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default:
		report_conn(conn, LOG_DEBUG,
			"%s  Feature: %d\n", prefix,
			attr->type);
		}
	}
}

void ks_chan_dump(
	struct ks_chan *chan,
	struct ks_conn *conn,
	int level)
{
	report_conn(conn, level, "  ID    : 0x%08x\n", chan->id);
	report_conn(conn, level, "  Path  : '%s'\n", chan->path);

	if (chan->from)
		report_conn(conn, level, "  From  : 0x%08x (%s)\n",
			chan->from->id, chan->from->path);

	if (chan->to)
		report_conn(conn, level, "  To    : 0x%08x (%s)\n",
			chan->to->id, chan->to->path);

	struct ks_feature_value *featval;
	list_for_each_entry(featval, &chan->features, node) {

		__u8 *text = alloca(featval->len * 2 + 1);

		int i;
		for(i=0; i<featval->len; i++)
			sprintf((char *)text + i * 2,
				"%02x", *(featval->payload + i));

		report_conn(conn, level,
			"  Feature: %s (%s)\n",
			featval->feature->name,
			text);
	}
}

int ks_chan_update(struct ks_chan *chan, struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_CHAN_SET;
	req->flags = NLM_F_REQUEST;

	req->skb = alloc_skb(4096, GFP_KERNEL);
	if (!req->skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	err = ks_netlink_put_attr(req->skb, KS_CHANATTR_ID,
			&chan->id,
			sizeof(chan->id));
	if (err < 0)
		goto err_put_attr_id;

	struct ks_feature_value *featval;
	list_for_each_entry(featval, &chan->features, node) {

		err = ks_netlink_put_attr(req->skb,
				featval->feature->id,
				featval->payload,
				featval->len);
		if (err < 0)
			goto err_put_attr_features;
	}

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
err_put_attr_features:
err_put_attr_id:
	/* skb is freed in req_put */
err_skb_alloc:
	ks_req_put(req);
err_req_alloc:

	return err;
}

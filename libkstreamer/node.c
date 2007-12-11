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

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/netlink.h>

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/node.h>
#include <libkstreamer/conn.h>
#include <libkstreamer/feature.h>
#include <libkstreamer/util.h>
#include <libkstreamer/logging.h>

#include <libkstreamer/pd_grammar.h>
#include <libkstreamer/pd_parser.h>

static inline struct hlist_head *ks_node_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->nodes_hash[id & (NODE_HASHSIZE - 1)];
}

void ks_node_add(struct ks_node *node, struct ks_conn *conn)
{
	node->conn = conn;

	hlist_add_head(
		&ks_node_get(node)->node,
		ks_node_get_hash(conn, node->id));
}

void ks_node_del(struct ks_node *node)
{
	hlist_del(&node->node);

	ks_node_put(node);
}

void ks_node_flush(struct ks_conn *conn)
{
	struct hlist_node *pos, *n;
	struct ks_node *node;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->nodes_hash); i++) {
		hlist_for_each_entry_safe(node, pos, n,
					&conn->nodes_hash[i], node) {

			hlist_del(&node->node);
			ks_node_put(node);
		}
	}
}

struct ks_node *_ks_node_get_by_id(
	struct ks_conn *conn,
	int id)
{
	struct ks_node *node;
	struct hlist_node *t;

	hlist_for_each_entry(node, t, ks_node_get_hash(conn, id), node) {
		if (node->id == id)
			return ks_node_get(node);
	}

	return NULL;
}

struct ks_node *ks_node_get_by_id(
	struct ks_conn *conn,
	int id)
{
	struct ks_node *node;

	pthread_rwlock_rdlock(&conn->topology_lock);
	node = _ks_node_get_by_id(conn, id);
	pthread_rwlock_unlock(&conn->topology_lock);

	return node;
}

static struct ks_node *_ks_node_get_by_path(
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

	struct ks_node *node;
	struct hlist_node *t;

	int i;
	for(i=0; i<ARRAY_SIZE(conn->nodes_hash); i++) {
		hlist_for_each_entry(node, t, &conn->nodes_hash[i], node) {
			if (!strcmp(node->path, sys_path)) {
				free(real_path);
				return ks_node_get(node);
			}
		}
	}

	free(real_path);

	return NULL;
}

struct ks_node *ks_node_get_by_path(
	struct ks_conn *conn,
	const char *path)
{
	struct ks_node *node;

	pthread_rwlock_rdlock(&conn->topology_lock);
	node = _ks_node_get_by_path(conn, path);
	pthread_rwlock_unlock(&conn->topology_lock);

	return node;
}

struct ks_node *ks_node_get_by_token(
	struct ks_conn *conn,
	struct ks_pd_token *token)
{
	struct ks_node *node;

	switch(token->id) {
	case TK_STRING:
		node = ks_node_get_by_path(conn, token->text);
	break;

	case TK_INTEGER:
		node = ks_node_get_by_id(conn, atoi(token->text));
	break;

	case TK_HEXINT: {
		int id;

		if (sscanf(token->text, "0x%x", &id) < 1)
			node = NULL;
		else
			node = ks_node_get_by_id(conn, id);
	}
	break;

	default:
		assert(0);
		node = NULL;
	}

	return node;
}

static __u32 ks_node_nlh_to_id(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_NODEATTR_ID)
			return *(__u32 *)KS_ATTR_DATA(attr);
	}

	return 0;
}

#if 0
const char *ks_netlink_node_attr_to_string(
		enum ks_node_attribute_type type)
{
	switch(type) {
	case KS_NODEATTR_ID:
		return "ID";
	case KS_NODEATTR_PATH:
		return "PATH";
	}

	return "*INVALID*";
}
#endif

struct ks_node *ks_node_alloc(void)
{
	struct ks_node *node;

	node = malloc(sizeof(*node));
	if (!node)
		return NULL;

	memset(node, 0, sizeof(*node));

	node->refcnt = 1;

	return node;
}

struct ks_node *ks_node_get(struct ks_node *node)
{
	assert(node->refcnt > 0);
	assert(node->refcnt < 100000);

	if (node) {
		pthread_mutex_lock(&refcnt_lock);
		node->refcnt++;
		pthread_mutex_unlock(&refcnt_lock);
	}

	return node;
}

void ks_node_put(struct ks_node *node)
{
	assert(node->refcnt > 0);
	assert(node->refcnt < 100000);

	pthread_mutex_lock(&refcnt_lock);
	int refcnt = --node->refcnt;
	pthread_mutex_unlock(&refcnt_lock);

	if (!refcnt) {
		int i;
		for (i=0; i<node->features_cnt; i++)
			ks_feature_put(node->features[i]);

		if (node->path)
			free(node->path);

		free(node);
	}
}

static struct ks_node *ks_node_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_node *node;

	node = ks_node_alloc();
	if (!node)
		return NULL;

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_NODEATTR_ID:
			node->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_NODEATTR_PATH:
			assert(!node->path);

			node->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		default: {
			struct ks_feature *feature;
			feature = _ks_feature_get_by_id(conn, attr->type);
			if (!feature) {
				report_conn(conn, LOG_ERR,
					"Attribute %d unknown\n", attr->type);
				break;
			}

			node->features[node->features_cnt++] =
						ks_feature_get(feature);
			ks_feature_put(feature);
		}
		}
	}

	return node;
}

static void ks_node_update_from_nlmsg(
	struct ks_node *node,
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_NODEATTR_ID:
		case KS_NODEATTR_PATH:

		default:
			// UNIMPLEMENTED FIXME
			assert(0);
		}
	}
}

/* Must be called holding topology_lock */

void ks_node_handle_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	switch(nlh->nlmsg_type) {
	case KS_NETLINK_NODE_NEW: {
		struct ks_node *node;

		node = ks_node_create_from_nlmsg(conn, nlh);
		if (!node) {
			// FIXME
		}

		ks_node_add(node, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_DEL: {
		__u32 id = ks_node_nlh_to_id(conn, nlh);
		struct ks_node *node = _ks_node_get_by_id(conn, id);
		if (!node) {
			report_conn(conn, LOG_ERR,
				"Node ID %08x not found!\n", id);
			break;
		}

		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);
		ks_node_del(node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_SET: {
		__u32 id = ks_node_nlh_to_id(conn, nlh);
		struct ks_node *node = _ks_node_get_by_id(conn, id);
		if (!node) {
			report_conn(conn, LOG_ERR,
				"Node ID %08x not found!\n", id);
			break;
		}

		ks_node_update_from_nlmsg(node, conn, nlh);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, node);

		ks_node_put(node);
	}
	break;
	}
}

void ks_node_nlmsg_dump(
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
		case KS_NODEATTR_ID:
			report_conn(conn, LOG_DEBUG,
				"%s  ID    : 0x%08x\n",	prefix,
				*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_NODEATTR_PATH:
			report_conn(conn, LOG_DEBUG,
				"%s  Path  : '%s'\n", prefix,
				strndupa(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr)));
		break;

		default:
		report_conn(conn, LOG_DEBUG,
			"%s  Feature: %d\n", prefix,
			attr->type);
		}
	}
}

void ks_node_dump(
	struct ks_node *node,
	struct ks_conn *conn,
	int level)
{
	report_conn(conn, level, "  ID    : 0x%08x\n", node->id);
	report_conn(conn, level, "  Path  : '%s'\n", node->path);

	int i;
	for (i=0; i<node->features_cnt; i++) {
		report_conn(conn, level,
			"  Feature: %s\n", node->features[i]->name);
	}
}


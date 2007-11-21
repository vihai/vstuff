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
#include <assert.h>

#include <linux/kstreamer/feature.h>
#include <linux/kstreamer/netlink.h>

#include "conn.h"
#include "feature.h"
#include "util.h"
#include "logging.h"

static inline struct hlist_head *ks_feature_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->features_hash[id & (FEATURE_HASHSIZE - 1)];
}

void ks_feature_add(struct ks_feature *feature, struct ks_conn *conn)
{
	feature->conn = conn;

	hlist_add_head(
		&ks_feature_get(feature)->node,
		ks_feature_get_hash(conn, feature->id));
}

void ks_feature_del(struct ks_feature *feature)
{
	hlist_del(&feature->node);

	ks_feature_put(feature);
}

void ks_feature_flush(struct ks_conn *conn)
{
	struct hlist_node *pos, *n;
	struct ks_feature *feature;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->features_hash); i++) {
		hlist_for_each_entry_safe(feature, pos, n,
					&conn->features_hash[i], node) {

			hlist_del(&feature->node);
			ks_feature_put(feature);
		}
	}
}

struct ks_feature *ks_feature_get_by_id(struct ks_conn *conn, int id)
{
	struct ks_feature *feature;
	struct hlist_node *t;

	hlist_for_each_entry(feature, t, ks_feature_get_hash(conn, id), node) {
		if (feature->id == id)
			return ks_feature_get(feature);
	}

	return NULL;
}

static struct ks_feature *ks_feature_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_FEATURE_ID)
			return ks_feature_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

struct ks_feature *ks_feature_get_by_name(
	struct ks_conn *conn,
	const char *name)
{
	struct ks_feature *feature;
	struct hlist_node *t;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->features_hash); i++) {
		hlist_for_each_entry(feature, t, &conn->features_hash[i],
								node) {
			if (!strcmp(feature->name, name))
				return ks_feature_get(feature);
		}
	}

	return NULL;
}

static const char *ks_netlink_feature_attr_to_string(
		enum ks_feature_attribute_type type)
{
	switch(type) {
	case KS_FEATURE_ID:
		return "ID";
	case KS_FEATURE_NAME:
		return "NAME";
	}

	return "*INVALID*";
}

struct ks_feature *ks_feature_alloc()
{
	struct ks_feature *feature;

	feature = malloc(sizeof(*feature));
	if (!feature)
		return NULL;

	memset(feature, 0, sizeof(*feature));

	feature->refcnt = 1;

	return feature;
}

struct ks_feature *ks_feature_get(struct ks_feature *feature)
{
	assert(feature->refcnt > 0);
	assert(feature->refcnt < 100000);

	if (feature)
		feature->refcnt++;

	return feature;
}

void ks_feature_put(struct ks_feature *feature)
{
	assert(feature->refcnt > 0);
	assert(feature->refcnt < 100000);

	feature->refcnt--;

	if (!feature->refcnt) {
		if (feature->name)
			free(feature->name);

		free(feature);
	}
}

static void ks_feature_update_from_nlmsg(
	struct ks_feature *feature,
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	attr = KS_ATTRS(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_FEATURE_ID:
		case KS_FEATURE_NAME:
		break;

		default:
			report_conn(conn, LOG_WARNING,
				"Attribute '%s' unknown\n",
				ks_netlink_feature_attr_to_string(
					attr->type));
		}
	}
}

static struct ks_feature *ks_feature_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_feature *feature;

	feature = ks_feature_alloc();
	if (!feature)
		return NULL;

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	attr = KS_ATTRS(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_FEATURE_ID:
			feature->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_FEATURE_NAME:
			assert(!feature->name);

			feature->name = strndup(KS_ATTR_DATA(attr),
						KS_ATTR_PAYLOAD(attr));
		break;

		default:
			report_conn(conn, LOG_WARNING,
				"Attribute '%s' unexpected\n",
				ks_netlink_feature_attr_to_string(
					attr->type));
		}
	}

	return feature;
}

void ks_feature_handle_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	switch(nlh->nlmsg_type) {
	case KS_NETLINK_FEATURE_NEW: {
		struct ks_feature *feature;
		feature = ks_feature_create_from_nlmsg(conn, nlh);
		if (!feature) {
			// FIXME
		}

		if (conn->debug_netlink)
			ks_feature_dump(feature, conn, LOG_DEBUG);

		ks_feature_add(feature, conn);
		ks_conn_topology_updated(conn, nlh->nlmsg_type, feature);
		ks_feature_put(feature);
	}
	break;

	case KS_NETLINK_FEATURE_DEL: {
		struct ks_feature *feature;

		feature = ks_feature_get_by_nlid(conn, nlh);
		if (!feature) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		if (conn->debug_netlink)
			ks_feature_dump(feature, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, feature);
		ks_feature_del(feature);
		ks_feature_put(feature);
	}
	break;

	case KS_NETLINK_FEATURE_SET: {
		struct ks_feature *feature;

		feature = ks_feature_get_by_nlid(conn, nlh);
		if (!feature) {
			report_conn(conn, LOG_ERR, "Sync lost\n");
			break;
		}

		ks_feature_update_from_nlmsg(feature, conn, nlh);

		if (conn->debug_netlink)
			ks_feature_dump(feature, conn, LOG_DEBUG);

		ks_conn_topology_updated(conn, nlh->nlmsg_type, feature);

		ks_feature_put(feature);
	}
	break;
	}
}

void ks_feature_dump(
	struct ks_feature *feature,
	struct ks_conn *conn,
	int level)
{
	report_conn(conn, level,
		"  ID    : 0x%08x\n"
		"  Name  : '%s'\n",
		feature->id,
		feature->name);
}


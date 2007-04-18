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

#include <linux/kstreamer/dynattr.h>
#include <linux/kstreamer/netlink.h>

#include "conn.h"
#include "dynattr.h"
#include "util.h"
#include "logging.h"

static inline struct hlist_head *ks_dynattr_get_hash(
	struct ks_conn *conn, int id)
{
	return &conn->dynattrs_hash[id & (DYNATTR_HASHSIZE - 1)];
}

void ks_dynattr_add(struct ks_dynattr *dynattr, struct ks_conn *conn)
{
	dynattr->conn = conn;

	hlist_add_head(
		&ks_dynattr_get(dynattr)->node,
		ks_dynattr_get_hash(conn, dynattr->id));
}

void ks_dynattr_del(struct ks_dynattr *dynattr)
{
	hlist_del(&dynattr->node);

	ks_dynattr_put(dynattr);
}

struct ks_dynattr *ks_dynattr_get_by_id(struct ks_conn *conn, int id)
{
	struct ks_dynattr *dynattr;
	struct hlist_node *t;

	hlist_for_each_entry(dynattr, t, ks_dynattr_get_hash(conn, id), node) {
		if (dynattr->id == id)
			return ks_dynattr_get(dynattr);
	}

	return NULL;
}

struct ks_dynattr *ks_dynattr_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		if(attr->type == KS_DYNATTR_ID)
			return ks_dynattr_get_by_id(conn,
					*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

struct ks_dynattr *ks_dynattr_get_by_name(
	struct ks_conn *conn,
	const char *name)
{
	struct ks_dynattr *dynattr;
	struct hlist_node *t;
	int i;

	for(i=0; i<ARRAY_SIZE(conn->dynattrs_hash); i++) {
		hlist_for_each_entry(dynattr, t, &conn->dynattrs_hash[i],
								node) {
			if (!strcmp(dynattr->name, name))
				return ks_dynattr_get(dynattr);
		}
	}

	return NULL;
}

static const char *ks_netlink_dynattr_attr_to_string(
		enum ks_dynattr_attribute_type type)
{
	switch(type) {
	case KS_DYNATTR_ID:
		return "ID";
	case KS_DYNATTR_NAME:
		return "NAME";
	}

	return "*INVALID*";
}

struct ks_dynattr *ks_dynattr_alloc()
{
	struct ks_dynattr *dynattr;

	dynattr = malloc(sizeof(*dynattr));
	if (!dynattr)
		return NULL;

	memset(dynattr, 0, sizeof(*dynattr));

	dynattr->refcnt = 1;

	return dynattr;
}

struct ks_dynattr *ks_dynattr_get(struct ks_dynattr *dynattr)
{
	assert(dynattr->refcnt > 0);

	if (dynattr)
		dynattr->refcnt++;

	return dynattr;
}

void ks_dynattr_put(struct ks_dynattr *dynattr)
{
	assert(dynattr->refcnt > 0);

	dynattr->refcnt--;

	if (!dynattr->refcnt)
		free(dynattr);
}

void ks_dynattr_update_from_nlmsg(
	struct ks_dynattr *dynattr,
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
		case KS_DYNATTR_ID:
		case KS_DYNATTR_NAME:
		break;

		default:
			report_conn(conn, LOG_WARNING,
				"Attribute '%s' unknown\n",
				ks_netlink_dynattr_attr_to_string(
					attr->type));
		}
	}
}

struct ks_dynattr *ks_dynattr_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_dynattr *dynattr;

	dynattr = ks_dynattr_alloc();
	if (!dynattr)
		return NULL;

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	attr = KS_ATTRS(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) {

		switch(attr->type) {
		case KS_DYNATTR_ID:
			dynattr->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_DYNATTR_NAME:
			if (dynattr->name)
				free(dynattr->name);

			dynattr->name = strndup(KS_ATTR_DATA(attr),
						KS_ATTR_PAYLOAD(attr));
		break;

		default:
			report_conn(conn, LOG_WARNING,
				"Attribute '%s' unexpected\n",
				ks_netlink_dynattr_attr_to_string(
					attr->type));
		}
	}

	return dynattr;
}

void ks_dynattr_dump(
	struct ks_dynattr *dynattr,
	struct ks_conn *conn,
	int level)
{
	report_conn(conn, level,
		"  ID    : 0x%08x\n"
		"  Name  : '%s'\n",
		dynattr->id,
		dynattr->name);
}


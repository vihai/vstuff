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
#include <assert.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/netlink.h>

#include "node.h"
#include "dynattr.h"
#include "util.h"

struct hlist_head ks_nodes_hash[NODE_HASHSIZE];

static inline struct hlist_head *ks_node_get_hash(int id)
{
	return &ks_nodes_hash[id & (NODE_HASHSIZE - 1)];
}

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

void ks_node_add(struct ks_node *node)
{
	hlist_add_head(
		&ks_node_get(node)->node,
		ks_node_get_hash(node->id));
}

void ks_node_del(struct ks_node *node)
{
	hlist_del(&node->node);

	ks_node_put(node);
}

struct ks_node *ks_node_get_by_path(const char *path)
{
	struct ks_node *node;
	struct hlist_node *t;

	int i;
	for(i=0; i<ARRAY_SIZE(ks_nodes_hash); i++) {
		hlist_for_each_entry(node, t, &ks_nodes_hash[i], node) {
			if (!strcmp(node->path, path))
				return ks_node_get(node);
		}
	}

	return NULL;
}

struct ks_node *ks_node_get_by_id(int id)
{
	struct ks_node *node;
	struct hlist_node *t;

	hlist_for_each_entry(node, t, ks_node_get_hash(id), node) {
		if (node->id == id)
			return ks_node_get(node);
	}

	return NULL;
}

struct ks_node *ks_node_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		if(attr->type == KS_NODEATTR_ID)
			return ks_node_get_by_id(*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

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

	if (node)
		node->refcnt++;

	return node;
}

void ks_node_put(struct ks_node *node)
{
	assert(node->refcnt > 0);

	node->refcnt--;

	if (!node->refcnt) {
		if (node->path)
			free(node->path);
		
		free(node);
	}
}

struct ks_node *ks_node_create_from_nlmsg(struct nlmsghdr *nlh)
{
	struct ks_node *node;

	node = ks_node_alloc();
	if (!node)
		return NULL;

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	attr = KS_ATTRS(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		switch(attr->type) {
		case KS_NODEATTR_ID:
			node->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_NODEATTR_PATH:
			if (node->path)
				free(node->path);

			node->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		default: {
			struct ks_dynattr *dynattr;
			dynattr = ks_dynattr_get_by_id(attr->type);
			if (!dynattr) {
				printf("   Attribute %d unknown\n", attr->type);
				break;
			}

			node->dynattrs[node->dynattrs_cnt++] = dynattr;
		}
		}
	}

	return node;
}

void ks_node_update_from_nlmsg(struct ks_node *node, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	attr = KS_ATTRS(nlh);

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

void ks_node_dump(struct ks_node *node)
{
	printf("  ID    : 0x%08x\n", node->id);
	printf("  Path  : '%s'\n", node->path);

	int i;
	for (i=0; i<node->dynattrs_cnt; i++) {
		printf("  Dynattr: %s\n", node->dynattrs[i]->name);
	}
}


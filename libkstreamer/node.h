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

#ifndef _LIBKSTREAMER_NODE_H
#define _LIBKSTREAMER_NODE_H

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/node.h>

#include <list.h>

#define NODE_HASHBITS 8
#define NODE_HASHSIZE ((1 << NODE_HASHBITS) - 1)

extern struct hlist_head ks_nodes_hash[NODE_HASHSIZE];

struct ks_node
{
	struct hlist_node node;

	int refcnt;

	__u32 id;

	char *path;

	struct ks_dynattr *dynattrs[16]; // FIXME
	int dynattrs_cnt;

	int router_cost;
	int router_done;

	struct ks_node *router_prev;
	struct ks_link *router_prev_thru;
};

struct ks_node *ks_node_alloc(void);
struct ks_node *ks_node_get(struct ks_node *node);
void ks_node_put(struct ks_node *node);

struct ks_node *ks_node_get_by_id(int id);
struct ks_node *ks_node_get_by_path(const char *path);
struct ks_node *ks_node_get_by_nlid(struct nlmsghdr *nlh);

void ks_node_dump(struct ks_node *node);

#ifdef _LIBKSTREAMER_PRIVATE_

const char *ks_netlink_node_attr_to_string(
		enum ks_node_attribute_type type);
void ks_node_add(struct ks_node *node);
void ks_node_del(struct ks_node *node);

void ks_node_update_from_nlmsg(struct ks_node *node, struct nlmsghdr *nlh);
struct ks_node *ks_node_create_from_nlmsg(struct nlmsghdr *nlh);

#endif

#endif

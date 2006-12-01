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
#include <sys/socket.h>
#include <linux/netlink.h>

#include <linux/kstreamer/node.h>

#include <list.h>

#include "pd_parser.h"

struct ks_conn;

struct ks_node
{
	struct hlist_node node;

	int refcnt;

	struct ks_conn *conn;

	__u32 id;

	char *path;

	struct ks_dynattr *dynattrs[16]; // FIXME
	int dynattrs_cnt;

	int router_cost;
	int router_done;

	struct ks_node *router_prev;
	struct ks_chan *router_prev_thru;
};

struct ks_node *ks_node_alloc(void);
struct ks_node *ks_node_get(struct ks_node *node);
void ks_node_put(struct ks_node *node);

struct ks_node *ks_node_get_by_id(
	struct ks_conn *conn,
	int id);
struct ks_node *ks_node_get_by_path(
	struct ks_conn *conn,
	const char *path);
struct ks_node *ks_node_get_by_token(
	struct ks_conn *conn,
	struct ks_pd_token *token);

void ks_node_dump(
	struct ks_node *node,
	struct ks_conn *conn);

#ifdef _LIBKSTREAMER_PRIVATE_

void ks_node_add(struct ks_node *node, struct ks_conn *conn);
void ks_node_del(struct ks_node *node);

struct ks_node *ks_node_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

struct ks_node *ks_node_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);
void ks_node_update_from_nlmsg(
	struct ks_node *node,
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

#endif

#endif

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

#ifndef _LIBKSTREAMER_CHANNEL_H
#define _LIBKSTREAMER_CHANNEL_H

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <list.h>

#include "pd_grammar.h"
#include "pd_parser.h"

struct ks_xact;
struct ks_conn;

struct ks_chan
{
	struct hlist_node node;

	int refcnt;

	struct ks_conn *conn;

	__u32 id;

	char *path;
	struct ks_node *from;
	struct ks_node *to;

	struct ks_pipeline *pipeline;

	struct list_head features;

	int router_cost;
	int router_done;

	struct ks_node *router_prev;
	struct ks_chan *router_prev_thru;

	int cost;
};

struct ks_chan *ks_chan_alloc(void);
struct ks_chan *ks_chan_get(struct ks_chan *chan);
void ks_chan_put(struct ks_chan *chan);

struct ks_chan *ks_chan_get_by_id(
	struct ks_conn *conn,
	int id);
struct ks_chan *ks_chan_get_by_path(
	struct ks_conn *conn,
	const char *path);
struct ks_chan *ks_chan_get_by_token(
	struct ks_conn *conn,
	struct ks_pd_token *token);

void ks_chan_dump(
	struct ks_chan *chan,
	struct ks_conn *conn,
	int level);

struct ks_req *ks_chan_queue_update(
	struct ks_chan *chan,
	struct ks_xact *xact);

#ifdef _LIBKSTREAMER_PRIVATE_

void ks_chan_add(struct ks_chan *chan, struct ks_conn *conn);
void ks_chan_del(struct ks_chan *chan);
void ks_chan_flush(struct ks_conn *conn);

struct ks_chan *ks_chan_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

int ks_chan_update(struct ks_chan *chan, struct ks_conn *conn);

struct ks_chan *ks_chan_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

void ks_chan_update_from_nlmsg(
	struct ks_chan *chan,
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

#endif

#endif

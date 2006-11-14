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

#ifndef _LIBKSTREAMER_CHANNEL_H
#define _LIBKSTREAMER_CHANNEL_H

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <list.h>

#include "conn.h"

#define CHAN_HASHBITS 8
#define CHAN_HASHSIZE ((1 << CHAN_HASHBITS) - 1)

extern struct hlist_head ks_chans_hash[CHAN_HASHSIZE];

struct ks_chan
{
	struct hlist_node node;

	int refcnt;

	__u32 id;

	char *path;
	struct ks_node *from;
	struct ks_node *to;

	struct list_head dynattrs;

	int router_cost;
	int router_done;

	struct ks_node *router_prev;
	struct ks_chan *router_prev_thru;

	int cost;
};

struct ks_chan *ks_chan_alloc(void);
struct ks_chan *ks_chan_get(struct ks_chan *chan);
void ks_chan_put(struct ks_chan *chan);

const char *ks_netlink_chan_attr_to_string(
		enum ks_chan_attribute_type type);

void ks_chan_add(struct ks_chan *chan);
void ks_chan_del(struct ks_chan *chan);
struct ks_chan *ks_chan_get_by_id(int id);
struct ks_chan *ks_chan_get_by_nlid(struct nlmsghdr *nlh);

void ks_chan_dump(struct ks_chan *chan);

struct ks_chan *ks_chan_create_from_nlmsg(struct nlmsghdr *nlh);
void ks_chan_update_from_nlmsg(struct ks_chan *chan, struct nlmsghdr *nlh);

struct ks_req *ks_chan_queue_update(
	struct ks_chan *chan,
	struct ks_xact *xact);

int ks_chan_update(struct ks_chan *chan, struct ks_conn *conn);

#endif

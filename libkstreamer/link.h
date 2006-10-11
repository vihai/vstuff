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

#ifndef _LIBKSTREAMER_LINK_H
#define _LIBKSTREAMER_LINK_H

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/link.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <list.h>

#define LINK_HASHBITS 8
#define LINK_HASHSIZE ((1 << LINK_HASHBITS) - 1)

extern struct hlist_head ks_links_hash[LINK_HASHSIZE];

struct ks_link
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
	struct ks_link *router_prev_thru;

	int cost;
};

struct ks_link *ks_link_alloc(void);
struct ks_link *ks_link_get(struct ks_link *link);
void ks_link_put(struct ks_link *link);

const char *ks_netlink_link_attr_to_string(
		enum ks_link_attribute_type type);

void ks_link_add(struct ks_link *link);
void ks_link_del(struct ks_link *link);
struct ks_link *ks_link_get_by_id(int id);
struct ks_link *ks_link_get_by_nlid(struct nlmsghdr *nlh);

void ks_link_dump(struct ks_link *link);

int ks_link_write_to_nlmsg(
	struct ks_link *link,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	__u32 pid, __u32 seq, __u16 flags);
struct ks_link *ks_link_create_from_nlmsg(struct nlmsghdr *nlh);
void ks_link_update_from_nlmsg(struct ks_link *link, struct nlmsghdr *nlh);

#endif

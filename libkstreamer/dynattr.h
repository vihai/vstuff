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

#ifndef _LIBKSTREAMER_DYNATTR_H
#define _LIBKSTREAMER_DYNATTR_H

#include <sys/socket.h>

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/dynattr.h>

#include <list.h>

struct ks_dynattr
{
	struct hlist_node node;

	int refcnt;

	__u32 id;
	char *name;
};

struct ks_dynattr_instance
{
	struct list_head node;

	struct ks_dynattr *dynattr;

	int len;

	__u8 payload[0];
};

struct ks_dynattr *ks_dynattr_alloc(void);
struct ks_dynattr *ks_dynattr_get(struct ks_dynattr *dynattr);
void ks_dynattr_put(struct ks_dynattr *dynattr);

const char *ks_netlink_dynattr_attr_to_string(
		enum ks_dynattr_attribute_type type);

void ks_dynattr_add(struct ks_dynattr *dynattr);
void ks_dynattr_del(struct ks_dynattr *dynattr);
struct ks_dynattr *ks_dynattr_get_by_id(int id);
struct ks_dynattr *ks_dynattr_get_by_nlid(struct nlmsghdr *nlh);
struct ks_dynattr *ks_dynattr_get_by_name(const char *name);

struct ks_dynattr *ks_dynattr_create_from_nlmsg(struct nlmsghdr *nlh);
void ks_dynattr_update_from_nlmsg(
	struct ks_dynattr *dynattr,
	struct nlmsghdr *nlh);

void ks_dynattr_dump(struct ks_dynattr *dynattr);

#endif

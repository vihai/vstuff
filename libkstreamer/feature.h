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

#ifndef _LIBKSTREAMER_FEATURE_H
#define _LIBKSTREAMER_FEATURE_H

#include <sys/socket.h>

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/feature.h>

#include <list.h>

struct ks_conn;

struct ks_feature
{
	struct hlist_node node;

	int refcnt;

	struct ks_conn *conn;

	__u32 id;
	char *name;
};

struct ks_feature_value
{
	struct list_head node;

	struct ks_feature *feature;

	int len;

	__u8 payload[0];
};

struct ks_feature *ks_feature_alloc(void);
struct ks_feature *ks_feature_get(struct ks_feature *feature);
void ks_feature_put(struct ks_feature *feature);

struct ks_feature *ks_feature_get_by_id(
	struct ks_conn *conn,
	int id);
struct ks_feature *ks_feature_get_by_name(
	struct ks_conn *conn,
	const char *name);

void ks_feature_dump(
	struct ks_feature *feature,
	struct ks_conn *conn,
	int level);

#ifdef _LIBKSTREAMER_PRIVATE_

struct ks_feature *_ks_feature_get_by_id(struct ks_conn *conn, int id);
struct ks_feature *_ks_feature_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

void ks_feature_add(struct ks_feature *feature, struct ks_conn *conn);
void ks_feature_del(struct ks_feature *feature);
void ks_feature_flush(struct ks_conn *conn);

void ks_feature_handle_topology_update(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

#endif

#endif

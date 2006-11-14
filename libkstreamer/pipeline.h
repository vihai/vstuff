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

#ifndef _LIBKSTREAMER_PIPELINE_H
#define _LIBKSTREAMER_PIPELINE_H

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>
#include <linux/kstreamer/pipeline.h>

#include <list.h>

#include <libskb.h>

#include "conn.h"

struct ks_pipeline
{
	struct hlist_node node;

	int refcnt;

	__u32 id;

	char *path;

	enum ks_pipeline_status status;

	struct ks_chan *chans[32];
	int chans_cnt;
};

struct ks_pipeline *ks_pipeline_alloc(void);
struct ks_pipeline *ks_pipeline_get(struct ks_pipeline *pipeline);
void ks_pipeline_put(struct ks_pipeline *pipeline);

void ks_pipeline_add(struct ks_pipeline *pipeline);
void ks_pipeline_del(struct ks_pipeline *pipeline);
struct ks_pipeline *ks_pipeline_get_by_id(int id);
struct ks_pipeline *ks_pipeline_get_by_nlid(struct nlmsghdr *nlh);
const char *ks_netlink_pipeline_attr_to_string(
		enum ks_pipeline_attribute_type type);
void ks_pipeline_dump(struct ks_pipeline *pipeline);
struct ks_pipeline *ks_pipeline_alloc(void);

int ks_pipeline_write_to_nlmsg(
	struct ks_pipeline *pipeline,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	__u32 pid, __u32 seq, __u16 flags);
struct ks_pipeline *ks_pipeline_create_from_nlmsg(struct nlmsghdr *nlh);
void ks_pipeline_update_from_nlmsg(
	struct ks_pipeline *pipeline,
	struct nlmsghdr *nlh);

int ks_pipeline_create(struct ks_pipeline *pipeline, struct ks_conn *conn);
int ks_pipeline_update(struct ks_pipeline *pipeline, struct ks_conn *conn);
int ks_pipeline_destroy(struct ks_pipeline *pipeline, struct ks_conn *conn);

int ks_pipeline_update_chans(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn);

#endif

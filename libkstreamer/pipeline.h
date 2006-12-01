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

#include "util.h"

struct ks_conn;

struct ks_pipeline
{
	struct hlist_node node;

	struct ks_conn *conn;

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

struct ks_pipeline *ks_pipeline_get_by_id(
	struct ks_conn *conn,
	int id);
struct ks_pipeline *ks_pipeline_get_by_path(
	struct ks_conn *conn,
	const char *path);
struct ks_pipeline *ks_pipeline_get_by_string(
	struct ks_conn *conn,
	const char *pipeline_str);

void ks_pipeline_dump(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn);

int ks_pipeline_create(struct ks_pipeline *pipeline, struct ks_conn *conn);
int ks_pipeline_update(struct ks_pipeline *pipeline, struct ks_conn *conn);
int ks_pipeline_destroy(struct ks_pipeline *pipeline, struct ks_conn *conn);

int ks_pipeline_update_chans(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn);

struct ks_node;
int ks_pipeline_autoroute(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn,
	struct ks_node *src_node,
	struct ks_node *dst_node);

struct ks_pipeline_par
{
	char *name;
	char *value;
};

#ifdef _LIBKSTREAMER_PRIVATE_

void ks_pipeline_add(struct ks_pipeline *pipeline, struct ks_conn *conn);
void ks_pipeline_del(struct ks_pipeline *pipeline);

struct ks_pipeline *ks_pipeline_get_by_nlid(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

struct ks_pipeline *ks_pipeline_create_from_nlmsg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

void ks_pipeline_update_from_nlmsg(
	struct ks_pipeline *pipeline,
	struct ks_conn *conn,
	struct nlmsghdr *nlh);

#endif

#endif

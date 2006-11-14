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

#ifndef _LIBKSTREAMER_XACT_H
#define _LIBKSTREAMER_XACT_H

#include <list.h>

#include "util.h"

#define KS_XACT_HASHBITS 4
#define KS_XACT_HASHSIZE ((1 << KS_XACT_HASHBITS) - 1)

enum ks_xact_state
{
	KS_XACT_STATE_NULL,
	KS_XACT_STATE_ACTIVE,
	KS_XACT_STATE_COMPLETED,
};

struct ks_conn;

struct ks_xact
{
	struct list_head node;

	int refcnt;

	int id;

	struct ks_conn *conn;

	struct sk_buff *out_skb;

	enum ks_xact_state state;

	struct list_head requests;
	struct list_head requests_sent;
	struct list_head requests_done;
};

struct ks_xact *ks_xact_alloc(struct ks_conn *conn);
struct ks_xact *ks_xact_get(struct ks_xact *xact);
void ks_xact_put(struct ks_xact *xact);

int ks_xact_begin(struct ks_xact *xact);
int ks_xact_commit(struct ks_xact *xact);
int ks_xact_abort(struct ks_xact *xact);

struct ks_req *ks_xact_queue_begin(struct ks_xact *xact);
struct ks_req *ks_xact_queue_commit(struct ks_xact *xact);
struct ks_req *ks_xact_queue_abort(struct ks_xact *xact);

void ks_xact_queue_request(
	struct ks_xact *xact,
	struct ks_req *req);
struct ks_req *ks_xact_queue_new_request(
		struct ks_xact *xact, __u16 type, __u16 flags);
struct ks_req *ks_xact_queue_new_request_callback(
	struct ks_xact *xact, __u16 type, __u16 flags,
	int (*callback)(struct ks_req *req, struct nlmsghdr *nlh, void *data));

int ks_xact_run(struct ks_xact *xact);

void ks_xact_waitloop(struct ks_xact *xact);
void ks_xact_need_skb(struct ks_xact *xact);

#endif

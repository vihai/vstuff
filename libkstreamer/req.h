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

#ifndef _LIBKSTREAMER_REQUEST_H
#define _LIBKSTREAMER_REQUEST_H

#include <list.h>

#include "util.h"

struct ks_xact;

struct ks_req
{
	struct list_head node;

	struct ks_xact *xact;

	int refcnt;

	int id;

	BOOL completed;
	int err;

	void *data;
	int (*response_callback)(struct ks_req *req, struct nlmsghdr *nlh,
				void *data);
	int (*request_fill_callback)(struct ks_req *req, struct sk_buff *skb,
					void *data);

	__u16 type;
	__u16 flags;

	void *response_data;
};

extern struct ks_req ks_nomem_request;

struct ks_req *ks_req_alloc(struct ks_xact *xact);
struct ks_req *ks_req_get(struct ks_req *req);
void ks_req_put(struct ks_req *req);
void ks_req_wait(struct ks_req *req);

#ifdef _LIBKSTREAMER_PRIVATE_

void ks_req_wait_default(struct ks_req *req);

#endif

#endif

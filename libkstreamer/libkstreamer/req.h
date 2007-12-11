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

#ifndef _LIBKSTREAMER_REQUEST_H
#define _LIBKSTREAMER_REQUEST_H

#include <pthread.h>

#include <list.h>

#include <libkstreamer/util.h>

struct ks_conn;

struct ks_req
{
	struct list_head node;

	struct ks_conn *conn;

	int refcnt;

	__u32 id;
	int multi_seq;

	struct ks_timer timer;

	pthread_mutex_t completed_lock;
	pthread_cond_t completed_cond;
	KSBOOL completed;

	int err;

	struct sk_buff *skb;
	int (*response_callback)(struct ks_req *req);
	void *response_data;

	__u16 type;
	__u16 flags;

	void *response_payload;
	int response_payload_size;
};

extern struct ks_req ks_nomem_request;

struct ks_req *ks_req_alloc(struct ks_conn *conn);
struct ks_req *ks_req_get(struct ks_req *req);
void ks_req_put(struct ks_req *req);
void ks_req_wait(struct ks_req *req);
void ks_req_complete(struct ks_req *req, int err);

#ifdef _LIBKSTREAMER_PRIVATE_

int ks_req_resp_append_payload(
	struct ks_req *req,
	struct nlmsghdr *nlh);

#endif

#endif

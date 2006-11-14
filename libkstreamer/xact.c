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

#define _GNU_SOURCE
#define _LIBKSTREAMER_PRIVATE_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <sys/socket.h>
#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include "conn.h"
#include "netlink.h"
#include "util.h"
#include "xact.h"
#include "req.h"

struct ks_xact *ks_xact_alloc(struct ks_conn *conn)
{
	struct ks_xact *xact;

	xact = malloc(sizeof(*xact));
	if (!xact)
		return NULL;

	memset(xact, 0, sizeof(*xact));

	INIT_LIST_HEAD(&xact->requests);
	INIT_LIST_HEAD(&xact->requests_sent);
	INIT_LIST_HEAD(&xact->requests_done);

	xact->refcnt = 1;
	xact->conn = conn;
	xact->id = conn->seqnum;
	xact->state = KS_XACT_STATE_NULL;

	return xact;
}

struct ks_xact *ks_xact_get(struct ks_xact *xact)
{
	assert(xact->refcnt > 0);
	assert(xact->refcnt < 100000);

	if (xact)
		xact->refcnt++;

	return xact;
}

void ks_xact_put(struct ks_xact *xact)
{
	assert(xact->refcnt > 0);
	assert(xact->refcnt < 100000);

	xact->refcnt--;

	if (!xact->refcnt)
		free(xact);
}

void ks_xact_queue_request(
	struct ks_xact *xact,
	struct ks_req *req)
{
	list_add_tail(&ks_req_get(req)->node, &xact->requests);
}

struct ks_req *ks_xact_queue_new_request(
	struct ks_xact *xact,
	__u16 type, __u16 flags)
{
	struct ks_req *req;
	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = type;
	req->flags = flags;
	req->response_callback = NULL;

	ks_xact_queue_request(xact, req);

	return req;
}

struct ks_req *ks_xact_queue_new_request_callback(
	struct ks_xact *xact,
	__u16 type,
	__u16 flags,
	int (*callback)(struct ks_req *req, struct nlmsghdr *nlh, void *data))
{
	struct ks_req *req;
	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = type;
	req->flags = flags;
	req->response_callback = callback;

	list_add_tail(&ks_req_get(req)->node, &xact->requests);

	return req;
}

int ks_xact_begin(struct ks_xact *xact)
{
	struct ks_req *req;
	req = ks_xact_queue_new_request(xact, KS_NETLINK_BEGIN,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_xact_run(xact);
	ks_req_waitloop(req);

	return 0;
}

int ks_xact_commit(struct ks_xact *xact)
{
	struct ks_req *req;
	req = ks_xact_queue_new_request(xact, KS_NETLINK_COMMIT,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_xact_run(xact);
	ks_req_waitloop(req);

	return 0;
}

int ks_xact_abort(struct ks_xact *xact)
{
	struct ks_req *req;
	req = ks_xact_queue_new_request(xact, KS_NETLINK_ABORT,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_xact_run(xact);
	ks_req_waitloop(req);

	return 0;
}

struct ks_req *ks_xact_queue_begin(struct ks_xact *xact)
{
	return ks_xact_queue_new_request(xact, KS_NETLINK_BEGIN,
						NLM_F_REQUEST);
}

struct ks_req *ks_xact_queue_commit(struct ks_xact *xact)
{
	return ks_xact_queue_new_request(xact, KS_NETLINK_COMMIT,
						NLM_F_REQUEST);
}

struct ks_req *ks_xact_queue_abort(struct ks_xact *xact)
{
	return ks_xact_queue_new_request(xact, KS_NETLINK_ABORT,
						NLM_F_REQUEST);
}

int ks_xact_run(struct ks_xact *xact)
{
	if (xact->state == KS_XACT_STATE_NULL) {
		ks_conn_add_xact(xact->conn, xact);

		xact->state = KS_XACT_STATE_ACTIVE;
	}

	ks_send_next_packet(xact->conn);

	return 0;
}

void ks_xact_waitloop(struct ks_xact *xact)
{
	while(xact->state != KS_XACT_STATE_COMPLETED)
		ks_netlink_receive(xact->conn);
}

void ks_xact_need_skb(struct ks_xact *xact)
{
	if (!xact->out_skb)
		xact->out_skb = alloc_skb(4096, GFP_KERNEL);
}

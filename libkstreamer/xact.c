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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <libkstreamer/conn.h>
#include <libkstreamer/util.h>
#include <libkstreamer/xact.h>
#include <libkstreamer/req.h>

struct ks_xact *ks_xact_alloc(struct ks_conn *conn)
{
	struct ks_xact *xact;

	xact = malloc(sizeof(*xact));
	if (!xact)
		return NULL;

	memset(xact, 0, sizeof(*xact));

	pthread_mutex_init(&xact->requests_lock, NULL);
	INIT_LIST_HEAD(&xact->requests);
	INIT_LIST_HEAD(&xact->requests_sent);

	xact->refcnt = 1;
	xact->conn = conn;
	xact->id = conn->seqnum;
	xact->autocommit = TRUE;

	xact->state = KS_XACT_STATE_NULL;
	pthread_mutex_init(&xact->state_lock, NULL);
	pthread_cond_init(&xact->state_cond, NULL);

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

	if (!xact->refcnt) {
		pthread_cond_destroy(&xact->state_cond);
		pthread_mutex_destroy(&xact->state_lock);
		pthread_mutex_destroy(&xact->requests_lock);

		free(xact);
	}
}

void ks_xact_queue_request(
	struct ks_xact *xact,
	struct ks_req *req)
{
	assert(xact->state != KS_XACT_STATE_COMPLETED);

	pthread_mutex_lock(&xact->requests_lock);
	list_add_tail(&ks_req_get(req)->node, &xact->requests);
	pthread_mutex_unlock(&xact->requests_lock);

	ks_conn_send_message(xact->conn, KS_CONN_MSG_REFRESH, NULL, 0);
}

struct ks_req *ks_xact_queue_new_request(
	struct ks_xact *xact,
	__u16 type, __u16 flags)
{
	assert(xact->state != KS_XACT_STATE_COMPLETED);

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
	int (*callback)(struct ks_req *req, struct nlmsghdr *nlh))
{
	struct ks_req *req;
	req = ks_req_alloc(xact);
	if (!req)
		return ks_req_get(&ks_nomem_request);

	req->type = type;
	req->flags = flags;
	req->response_callback = callback;

	pthread_mutex_lock(&xact->requests_lock);
	list_add_tail(&ks_req_get(req)->node, &xact->requests);
	pthread_mutex_unlock(&xact->requests_lock);

	return req;
}

int ks_xact_begin(struct ks_xact *xact)
{
	struct ks_req *req;
	req = ks_xact_queue_new_request(xact, KS_NETLINK_BEGIN,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_xact_submit(xact);
	ks_req_wait(req);
	ks_req_put(req);

	return 0;
}

int ks_xact_commit(struct ks_xact *xact)
{
	struct ks_req *req;
	req = ks_xact_queue_new_request(xact, KS_NETLINK_COMMIT,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_req_wait(req);
	ks_req_put(req);

	return 0;
}

int ks_xact_abort(struct ks_xact *xact)
{
	struct ks_req *req;

	req = ks_xact_queue_new_request(xact, KS_NETLINK_ABORT,
						NLM_F_REQUEST);
	if (req->err < 0)
		return req->err;

	ks_req_wait(req);
	ks_req_put(req);

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

int ks_xact_submit(struct ks_xact *xact)
{
	assert(xact->state == KS_XACT_STATE_NULL);

	ks_conn_add_xact(xact->conn, xact);
	xact->state = KS_XACT_STATE_ACTIVE;

	ks_conn_send_message(xact->conn, KS_CONN_MSG_REFRESH, NULL, 0);

	return 0;
}

void ks_xact_wait(struct ks_xact *xact)
{
	pthread_mutex_lock(&xact->state_lock);
	while(xact->state != KS_XACT_STATE_COMPLETED) {
		pthread_cond_wait(&xact->state_cond, &xact->state_lock);
	}
	pthread_mutex_unlock(&xact->state_lock);
}

void ks_xact_need_skb(struct ks_xact *xact)
{
	if (!xact->out_skb)
		xact->out_skb = alloc_skb(4096, GFP_KERNEL);
}

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
#include <errno.h>
#include <assert.h>

#include <libkstreamer/conn.h>
#include <libkstreamer/util.h>
#include <libkstreamer/req.h>
#include <libkstreamer/logging.h>

struct ks_req ks_nomem_request =
{
	.refcnt = 1,
	.completed = TRUE,
	.err = -ENOMEM,
};

void ks_req_timer(void *data)
{
	struct ks_req *req = data;

	report_conn(req->conn, LOG_ERR,
		"Timeout waiting for request processin!!\n");

	ks_req_complete(req, -ETIMEDOUT);
}

struct ks_req *ks_req_alloc(struct ks_conn *conn)
{
	struct ks_req *req;

	req = malloc(sizeof(*req));
	if (!req)
		return NULL;

	memset(req, 0, sizeof(*req));

	req->refcnt = 1;
	req->conn = conn;
	req->id = 0;
	req->multi_seq = 1;

	ks_timer_init(&req->timer, &conn->timerset, "ks_req",
		ks_req_timer, req);

	pthread_mutex_init(&req->completed_lock, NULL);
	pthread_cond_init(&req->completed_cond, NULL);

	return req;
}

struct ks_req *ks_req_get(struct ks_req *req)
{
	assert(req->refcnt > 0);

	if (req)
		req->refcnt++;

	return req;
}

void ks_req_put(struct ks_req *req)
{
	assert(req->refcnt > 0);

	req->refcnt--;

	if (!req->refcnt) {

		pthread_mutex_destroy(&req->completed_lock);
		pthread_cond_destroy(&req->completed_cond);

		if (req->response_payload) {
			free(req->response_payload);
			req->response_payload = NULL;
		}

		if (req->skb)
			kfree_skb(req->skb);

		free(req);
	}
}

void ks_req_complete(struct ks_req *req, int err)
{
	req->err = err;

	ks_timer_stop(&req->timer);

	pthread_mutex_lock(&req->completed_lock);
	req->completed = TRUE;
	pthread_mutex_unlock(&req->completed_lock);
	pthread_cond_broadcast(&req->completed_cond);

	if (req->response_callback)
		req->response_callback(req);
}

void ks_req_wait(struct ks_req *req)
{
	pthread_mutex_lock(&req->completed_lock);
	while(!req->completed) {
		pthread_cond_wait(&req->completed_cond, &req->completed_lock);
	}
	pthread_mutex_unlock(&req->completed_lock);
}

int ks_req_resp_append_payload(
	struct ks_req *req,
	struct nlmsghdr *nlh)
{
	req->response_payload = realloc(req->response_payload,
					req->response_payload_size +
					NLMSG_ALIGN(nlh->nlmsg_len));

	if (!req->response_payload)
		return -ENOMEM;

	memcpy(req->response_payload + req->response_payload_size,
				nlh, NLMSG_ALIGN(nlh->nlmsg_len));

	req->response_payload_size += NLMSG_ALIGN(nlh->nlmsg_len);

	return 0;
}



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

#include "conn.h"
#include "netlink.h"
#include "util.h"
#include "req.h"
#include "xact.h"

struct ks_req ks_nomem_request =
{
	.refcnt = 1,
	.completed = TRUE,
	.err = -ENOMEM,
};

struct ks_req *ks_req_alloc(struct ks_xact *xact)
{
	struct ks_req *req;

	req = malloc(sizeof(*req));
	if (!req)
		return NULL;

	memset(req, 0, sizeof(*req));

	req->refcnt = 1;
	req->xact = xact;
	req->id = xact->conn->seqnum++;

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

		if (req->response_data) {
			free(req->response_data);
			req->response_data = NULL;
		}

		free(req);
	}
}

void ks_req_wait(struct ks_req *req)
{
	pthread_mutex_lock(&req->completed_lock);
	while(!req->completed) {
		pthread_cond_wait(&req->completed_cond, &req->completed_lock);
	}
	pthread_mutex_unlock(&req->completed_lock);
}

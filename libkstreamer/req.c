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
		if (req->response_data) {
			free(req->response_data);
			req->response_data = NULL;
		}

		free(req);
	}
}

void ks_req_waitloop(struct ks_req *req)
{
	while(!req->completed)
		ks_netlink_receive(req->xact->conn);
}

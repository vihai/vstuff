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
#include <assert.h>

#include "request.h"
#include "conn.h"
#include "util.h"

struct ks_request *ks_request_alloc(struct ks_conn *conn)
{
	struct ks_request *request;
	
	request = malloc(sizeof(*request));
	if (!request)
		return NULL;
	
	memset(request, 0, sizeof(*request));

	request->refcnt = 1;
	request->seqnum = conn->seqnum;

	conn->seqnum++;

	return request;
}

struct ks_request *ks_request_get(struct ks_request *request)
{
	assert(request->refcnt > 0);

	if (request)
		request->refcnt++;

	return request;
}

void ks_request_put(struct ks_request *request)
{
	assert(request->refcnt > 0);

	request->refcnt--;

	if (!request->refcnt)
		free(request);
}


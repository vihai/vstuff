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

#define KS_REQUEST_HASHBITS 4
#define KS_REQUEST_HASHSIZE ((1 << KS_REQUEST_HASHBITS) - 1)

struct ks_conn;

struct ks_request
{
	struct list_head node;

	int refcnt;

	int seqnum;

	void *object;
};

struct ks_request *ks_request_alloc(struct ks_conn *conn);
struct ks_request *ks_request_get(struct ks_request *req);
void ks_request_put(struct ks_request *req);

#endif

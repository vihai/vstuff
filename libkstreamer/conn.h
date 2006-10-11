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

#ifndef _LIBKSTREAMER_CONN_H
#define _LIBKSTREAMER_CONN_H

//#include "request.h"
#include <list.h>

enum ks_conn_state
{
	KS_STATE_NULL,
	KS_STATE_SYNCING,
	KS_STATE_IDLE,
};

struct ks_conn
{
	enum ks_conn_state state;

	int sock;

	int seqnum;

	int dump_packets;

	struct list_head requests;
};

struct ks_request;

struct ks_conn *ks_conn_create(void);
void ks_conn_destroy(struct ks_conn *conn);
void ks_conn_add_request(struct ks_conn *conn, struct ks_request *req);

#endif

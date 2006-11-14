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

#include <linux/types.h>
#include <list.h>

#include <linux/kstreamer/netlink.h>

enum ks_topology_state
{
	KS_TOPOLOGY_STATE_NULL,
	KS_TOPOLOGY_STATE_SYNCING,
	KS_TOPOLOGY_STATE_SYNCHED,
};

enum ks_conn_state
{
	KS_CONN_STATE_NULL,
	KS_CONN_STATE_IDLE,
	KS_CONN_STATE_WAITING_ACK,
	KS_CONN_STATE_WAITING_DONE,
};

struct ks_conn
{
	enum ks_topology_state topology_state;

	enum ks_conn_state state;

	int sock;

	struct ks_netlink_version_response version;

	int seqnum;
	__u32 pid;

	int dump_packets;

	struct list_head xacts;

	struct ks_xact *cur_xact;
	struct ks_req *cur_req;
};

struct ks_req;

struct ks_conn *ks_conn_create(void);
void ks_conn_destroy(struct ks_conn *conn);
void ks_conn_add_xact(struct ks_conn *conn, struct ks_xact *xact);

void ks_conn_set_state(
	struct ks_conn *conn,
	enum ks_conn_state state);

#endif

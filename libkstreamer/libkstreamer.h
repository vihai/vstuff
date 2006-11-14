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

#ifndef _LIBKSTREAMER_H
#define _LIBKSTREAMER_H

#include "conn.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"
#include "dynattr.h"
#include "router.h"

#define KS_LIB_VERSION_MAJOR 0
#define KS_LIB_VERSION_MINOR 0
#define KS_LIB_VERSION_SERVICE 0

void ks_update_topology(struct ks_conn *conn);

/*struct ks_xact *ks_send_topology_update_req(
	struct ks_conn *conn, int *err);*/

int ks_send_noop(struct ks_conn *conn);

struct ks_pipeline *ks_connect(
	struct ks_conn *conn,
	struct ks_node *src_node,
	struct ks_node *dst_node,
	int *err);

#endif

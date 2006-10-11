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
#include "link.h"
#include "pipeline.h"
#include "dynattr.h"
#include "router.h"

void ks_update_topology(struct ks_conn *conn);

struct ks_pipeline *ks_connect(
	struct ks_conn *conn,
	struct ks_node *src_node,
	struct ks_node *dst_node,
	int *err);

#endif

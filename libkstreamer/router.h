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

#ifndef _LIBKSTREAMER_ROUTER_H
#define _LIBKSTREAMER_ROUTER_H

#include "node.h"

void router_run(struct ks_node *start, struct ks_node *to);

#endif

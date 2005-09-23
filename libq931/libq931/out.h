/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#ifndef _LIBQ931_OUT_H
#define _LIBQ931_OUT_H

#include <libq931/call.h>

int q931_send_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

int q931_send_message_bc(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

int q931_global_send_message(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

#endif

/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_INPUT_H
#define _LIBQ931_INPUT_H

#include <libq931/dlc.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/ccb.h>

#define	Q931_RECEIVE_OK		0
#define Q931_RECEIVE_REFRESH	1

int q931_receive(struct q931_dlc *dlc);
struct q931_dlc *q931_accept(
	struct q931_interface *intf,
	int accept_socket);

#ifdef Q931_PRIVATE

void q931_dl_establish_confirm(struct q931_dlc *dlc);
void q931_dl_establish_indication(struct q931_dlc *dlc);
void q931_dl_release_confirm(struct q931_dlc *dlc);
void q931_dl_release_indication(struct q931_dlc *dlc);

int q931_decode_information_elements(
	struct q931_call *call,
	struct q931_message *msg);

#endif

#endif

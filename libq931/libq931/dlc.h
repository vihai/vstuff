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

#ifndef _LIBQ931_DLC_H
#define _LIBQ931_DLC_H

#include <libq931/list.h>
#include <libq931/timer.h>

#define report_dlc(dlc, lvl, format, arg...)				\
	(dlc)->intf->lib->report(					\
		(lvl),							\
		"%s:TEI[%d]: "						\
		format,							\
		(dlc)->intf->name,					\
		(dlc)->tei,						\
		## arg)

enum q931_dlc_status
{
	Q931_DLC_STATUS_DISCONNECTED,
	Q931_DLC_STATUS_AWAITING_CONNECTION,
	Q931_DLC_STATUS_AWAITING_DISCONNECTION,
	Q931_DLC_STATUS_CONNECTED,
};

struct q931_interface;
struct q931_dlc
{
	int refcnt;

	struct list_head intf_node;

	int socket;
	struct q931_interface *intf;
	enum q931_dlc_status status;
	int tei;

	struct q931_timer autorelease_timer;

	struct list_head outgoing_queue;
};

struct q931_broadcast_dlc
{
	int socket;
	struct q931_interface *intf;
};

#ifdef Q931_PRIVATE

void q931_dlc_init(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket);
void q931_dlc_get(struct q931_dlc *dlc);
void q931_dlc_put(struct q931_dlc *dlc);

#endif

#endif

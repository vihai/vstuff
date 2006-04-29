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

#ifndef _LIBQ931_DLC_H
#define _LIBQ931_DLC_H

#include <libq931/list.h>
#include <libq931/timer.h>

#define report_dlc(dlc, lvl, format, arg...)				\
		q931_report(						\
			(lvl),						\
			"%s:TEI[%d]: "					\
			format,						\
			(dlc)->intf->name,				\
			(dlc)->tei,					\
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
	struct list_head intf_node;

	struct q931_interface *intf;
	int socket;

	int holdcnt;
	int refcnt;

	enum q931_dlc_status status;
	int tei;

	struct q931_timer autorelease_timer;

	struct list_head outgoing_queue;
};

#ifdef Q931_PRIVATE

void q931_dlc_init(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket);

void q931_broadcast_dlc_init(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket);

struct q931_dlc *_q931_dlc_get(struct q931_dlc *dlc, const char *file, int line);
void _q931_dlc_put(struct q931_dlc *dlc, const char *file, int line);
#define q931_dlc_get(dlc) _q931_dlc_get((dlc), __FILE__,  __LINE__)
#define q931_dlc_put_nonull(dlc) _q931_dlc_put((dlc), __FILE__,  __LINE__)
#define q931_dlc_put(dlc)	do { q931_dlc_put_nonull(dlc); dlc = NULL; } while(0)

void _q931_dlc_hold(struct q931_dlc *dlc, const char *file, int line);
void _q931_dlc_release(struct q931_dlc *dlc, const char *file, int line);
#define q931_dlc_hold(dlc) _q931_dlc_hold((dlc), __FILE__,  __LINE__)
#define q931_dlc_release(dlc) _q931_dlc_release((dlc), __FILE__,  __LINE__)

#endif

#endif

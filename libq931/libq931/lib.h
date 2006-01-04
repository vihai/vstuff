/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_LIB_H
#define _LIBQ931_LIB_H

#include <libq931/dlc.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/ccb.h>

enum q931_mode
{
	UNKNOWN_MODE,
	CIRCUIT_MODE,
	PACKET_MODE
};

struct q931_lib
{
	struct list_head timers;
	struct list_head intfs;

	void *pvt;

	void (*report)(int level, const char *format, ...);
	void (*timer_update)(struct q931_lib *lib);
	void (*queue_primitive)(
		struct q931_call *call,
		enum q931_primitive primitive,
		const struct q931_ies *ies,
		unsigned long par1,
		unsigned long par2);
};

static inline void q931_set_logger_func(
	struct q931_lib *lib,
	void (*report)(int level, const char *format, ...))
{
	lib->report = report;
}

#define	Q931_RECEIVE_OK		0
#define Q931_RECEIVE_REFRESH	1

struct q931_lib *q931_init();
void q931_leave(struct q931_lib *lib);
int q931_receive(struct q931_dlc *dlc);
struct q931_dlc *q931_accept(
	struct q931_interface *intf,
	int accept_socket);

#ifdef Q931_PRIVATE

#define report_lib(lib, lvl, format, arg...)		\
	(lib)->report(					\
		(lvl),					\
		format,					\
		## arg)

typedef char BOOL;

void q931_dl_establish_confirm(struct q931_dlc *dlc);
void q931_dl_establish_indication(struct q931_dlc *dlc);
void q931_dl_release_confirm(struct q931_dlc *dlc);
void q931_dl_release_indication(struct q931_dlc *dlc);

#endif

#endif

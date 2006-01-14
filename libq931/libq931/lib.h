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

void q931_set_report_func(
	void (*report_func)(int level, const char *format, ...));

void q931_set_timer_update_func(
	void (*timer_update_func)(void));

void q931_set_queue_primitive_func(
	void (*queue_primitive_func)(
		struct q931_call *call,
		enum q931_primitive primitive,
		const struct q931_ies *ies,
		unsigned long par1,
		unsigned long par2));

void q931_init();
void q931_leave();

#ifdef Q931_PRIVATE

extern struct list_head q931_timers;
extern struct list_head q931_interfaces;

extern void (*q931_report)(int level, const char *format, ...);
extern void (*q931_timer_update)(void);
extern void (*q931_queue_primitive)(
	struct q931_call *call,
	enum q931_primitive primitive,
	const struct q931_ies *ies,
	unsigned long par1,
	unsigned long par2);

typedef char BOOL;

#endif

#endif

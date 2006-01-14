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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <stdarg.h>

#include <linux/lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/output.h>
#include <libq931/input.h>
#include <libq931/global.h>
#include <libq931/ces.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/proto.h>

#include <libq931/ie_cause.h>
#include <libq931/ie_channel_identification.h>

#include "call_inline.h"

struct list_head q931_timers;
struct list_head q931_interfaces;

static void q931_default_report(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

void (*q931_report)(int level, const char *format, ...) = q931_default_report;
void (*q931_timer_update)() = NULL;
void (*q931_queue_primitive)(
	struct q931_call *call,
	enum q931_primitive primitive,
	const struct q931_ies *ies,
	unsigned long par1,
	unsigned long par2) = NULL;

void q931_set_report_func(
	void (*report_func)(int level, const char *format, ...))
{
	q931_report = report_func;
}

void q931_set_timer_update_func(
	void (*timer_update_func)(void))
{
	q931_timer_update = timer_update_func;
}

void q931_set_queue_primitive_func(
	void (*queue_primitive_func)(
		struct q931_call *call,
		enum q931_primitive primitive,
		const struct q931_ies *ies,
		unsigned long par1,
		unsigned long par2))
{
	q931_queue_primitive = queue_primitive_func;
}

void q931_init()
{
	q931_ie_classes_init();
	q931_message_types_init();

	INIT_LIST_HEAD(&q931_timers);
	INIT_LIST_HEAD(&q931_interfaces);
}

void q931_leave()
{
}

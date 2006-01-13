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

void q931_default_report(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

struct q931_lib *q931_init()
{
	struct q931_lib *lib;

	lib = malloc(sizeof(*lib));
	if (!lib)
		return NULL;

	lib->report = q931_default_report;

	// Non-reentrant, FIXME
	q931_ie_classes_init();
	q931_message_types_init();

	INIT_LIST_HEAD(&lib->timers);
	INIT_LIST_HEAD(&lib->intfs);

	return lib;
}

void q931_leave(struct q931_lib *lib)
{
	free(lib);
}

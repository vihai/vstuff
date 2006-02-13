/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2006 Daniele Orlandi
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

#define Q931_PRIVATE

#include <assert.h>

#include <libq931/lib.h>
#include <libq931/intf.h>
#include <libq931/logging.h>
#include <libq931/dummy.h>
#include <libq931/input.h>
#include <libq931/output.h>
#include <libq931/chanset.h>

#include <libq931/ie_call_state.h>
#include <libq931/ie_cause.h>

#if 0
static void q931_dummy_handle_notify(
	struct q931_message *msg)
{
}

static void q931_dummy_handle_facility(
	struct q931_message *msg)
{
}
#endif

void q931_dispatch_dummy_message(
	struct q931_message *msg)
{
	switch(msg->message_type) {
#if 0
	case Q931_MT_NOTIFY:
		q931_dummy_handle_notify(gc, msg);
	break;

	case Q931_MT_FACILITY:
		q931_dummy_handle_facility(gc, msg);
	break;
#endif

	default:
	break;
	}
}

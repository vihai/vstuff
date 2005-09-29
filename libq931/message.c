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

#define Q931_PRIVATE

#include <unistd.h>
#include <assert.h>

#include <libq931/list.h>
#include <libq931/lib.h>
#include <libq931/intf.h>
#include <libq931/logging.h>
#include <libq931/message.h>

struct q931_message *q931_message_get(
	struct q931_message *message)
{
	message->refcnt++;

	return message;
}

void q931_message_put(
	struct q931_message *message)
{
	assert(message);
	assert(message->refcnt > 0);

	message->refcnt--;

	if (message->refcnt == 0) {
		report_msg(message, LOG_DEBUG, "Releasing message\n");

		free(message);
	}
}

void q931_message_init(
	struct q931_message *message,
	struct q931_dlc *dlc)
{
	assert(message);
	assert(dlc);

	memset(message, 0, sizeof(*message));

	message->refcnt = 1;

	message->dlc = dlc;
}


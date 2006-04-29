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

#define Q931_PRIVATE

#include <unistd.h>
#include <assert.h>

#include <libq931/list.h>
#include <libq931/lib.h>
#include <libq931/intf.h>
#include <libq931/logging.h>
#include <libq931/message.h>

struct q931_message *q931_msg_get(
	struct q931_message *msg)
{
	msg->refcnt++;

	return msg;
}

void _q931_msg_put(
	struct q931_message *msg)
{
	assert(msg);
	assert(msg->refcnt > 0);

	msg->refcnt--;

	if (msg->refcnt == 0) {
		report_msg(msg, LOG_DEBUG, "Releasing message\n");

		if (msg->dlc)
			q931_dlc_put(msg->dlc);

		free(msg);
	}
}

struct q931_message *q931_msg_alloc(
	struct q931_dlc *dlc)
{
	struct q931_message *msg;

	assert(dlc);

	msg = malloc(sizeof(*msg));
	if (!msg)
		return NULL;

	memset(msg, 0, sizeof(*msg));

	msg->refcnt = 1;
	msg->dlc = q931_dlc_get(dlc);

	return msg;
}

struct q931_message *q931_msg_alloc_nodlc(void)
{
	struct q931_message *msg;

	msg = malloc(sizeof(*msg));
	if (!msg)
		return NULL;

	memset(msg, 0, sizeof(*msg));

	msg->refcnt = 1;
	msg->dlc = NULL;

	return msg;
}

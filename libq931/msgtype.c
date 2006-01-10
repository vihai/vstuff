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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include <libq931/util.h>
#include <libq931/msgtype.h>

static struct q931_message_type_name q931_message_type_names[] =
{
	{ Q931_MT_ALERTING,		"ALERTING" },
	{ Q931_MT_CALL_PROCEEDING,	"CALL PROCEEDING" },
	{ Q931_MT_CONNECT,		"CONNECT" },
	{ Q931_MT_CONNECT_ACKNOWLEDGE,	"CONNECT ACKNOWLEDGE" },
	{ Q931_MT_PROGRESS,		"PROGRESS" },
	{ Q931_MT_SETUP,		"SETUP" },
	{ Q931_MT_SETUP_ACKNOWLEDGE,	"SETUP ACKNOWLEDGE" },
	{ Q931_MT_DISCONNECT,		"DISCONNECT" },
	{ Q931_MT_RELEASE,		"RELEASE" },
	{ Q931_MT_RELEASE_COMPLETE,	"RELEASE COMPLETE" },
	{ Q931_MT_RESTART,		"RESTART" },
	{ Q931_MT_RESTART_ACKNOWLEDGE,	"RESTART ACKNOWLEDGE" },
	{ Q931_MT_STATUS,		"STATUS" },
	{ Q931_MT_STATUS_ENQUIRY,	"STATUS ENQUIRY" },
	{ Q931_MT_USER_INFORMATION,	"USER INFORMATION" },
	{ Q931_MT_SEGMENT,		"SEGMENT" },
	{ Q931_MT_CONGESTION_CONTROL,	"CONGESTION CONTROL" },
	{ Q931_MT_INFORMATION,		"INFORMATION" },
	{ Q931_MT_FACILITY,		"FACILITY" },
	{ Q931_MT_NOTIFY,		"NOTIFY" },
	{ Q931_MT_HOLD,			"HOLD" },
	{ Q931_MT_HOLD_ACKNOWLEDGE,	"HOLD ACKNOWLEDGE" },
	{ Q931_MT_HOLD_REJECT,		"HOLD REJECT" },
	{ Q931_MT_RETRIEVE,		"RETRIEVE" },
	{ Q931_MT_RETRIEVE_ACKNOWLEDGE,	"RETRIEVE ACKNOWLEDGE" },
	{ Q931_MT_RETRIEVE_REJECT,	"RETRIEVE REJECT" },
	{ Q931_MT_RESUME,		"RESUME" },
	{ Q931_MT_RESUME_ACKNOWLEDGE,	"RESUME ACKNOWLEDGE" },
	{ Q931_MT_RESUME_REJECT,	"RESUME REJECT" },
	{ Q931_MT_SUSPEND,		"SUSPEND" },
	{ Q931_MT_SUSPEND_ACKNOWLEDGE,	"SUSPEND ACKNOWLEDGE" },
	{ Q931_MT_SUSPEND_REJECT,	"SUSPEND REJECT" },
};

static int q931_message_type_id_compare(const void *a, const void *b)
{
	return q931_intcmp(((struct q931_message_type_name *)a)->id,
			((struct q931_message_type_name *)b)->id);
}

const char *q931_message_type_to_text(int id)
{
	struct q931_message_type_name key, *res;

	key.id = id;
	key.name = NULL;

	res = (struct q931_message_type_name *)
		bsearch(&key,
			q931_message_type_names,
			ARRAY_SIZE(q931_message_type_names),
			sizeof(struct q931_message_type_name),
			q931_message_type_id_compare);

	if (res)
		return res->name;
	else
		return NULL;
}

void q931_message_types_init()
{
	qsort(q931_message_type_names,
		ARRAY_SIZE(q931_message_type_names),
		sizeof(struct q931_message_type_name),
		q931_message_type_id_compare);
}

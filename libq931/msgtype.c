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
  { Q931_MT_ALERTING,			"Alerting" },
  { Q931_MT_CALL_PROCEEDING,		"Call Proceeding" },
  { Q931_MT_CONNECT,			"Connect" },
  { Q931_MT_CONNECT_ACKNOWLEDGE,	"Connect Acknowledge" },
  { Q931_MT_PROGRESS,			"Progress" },
  { Q931_MT_SETUP,			"Setup" },
  { Q931_MT_SETUP_ACKNOWLEDGE,		"Setup Acknowledge" },
  { Q931_MT_DISCONNECT,			"Disconnect" },
  { Q931_MT_RELEASE,			"Release" },
  { Q931_MT_RELEASE_COMPLETE,		"Release Complete" },
  { Q931_MT_RESTART,			"Restart" },
  { Q931_MT_RESTART_ACKNOWLEDGE,	"Restart Acknowledge" },
  { Q931_MT_STATUS,			"Status" },
  { Q931_MT_STATUS_ENQUIRY,		"Status_enquiry" },
  { Q931_MT_USER_INFORMATION,		"User Information" },
  { Q931_MT_SEGMENT,			"Segment" },
  { Q931_MT_CONGESTION_CONTROL,		"Congestion Control" },
  { Q931_MT_INFORMATION,		"Information" },
  { Q931_MT_FACILITY,			"Facility" },
  { Q931_MT_NOTIFY,			"Notify" },
  { Q931_MT_HOLD,			"Hold" },
  { Q931_MT_HOLD_ACKNOWLEDGE,		"Hold Acknowledge" },
  { Q931_MT_HOLD_REJECT,		"Hold Reject" },
  { Q931_MT_RETRIEVE,			"Retrieve" },
  { Q931_MT_RETRIEVE_ACKNOWLEDGE,	"Retrieve Acknowledge" },
  { Q931_MT_RETRIEVE_REJECT,		"Retrieve Reject" },
  { Q931_MT_RESUME,			"Resume" },
  { Q931_MT_RESUME_ACKNOWLEDGE,		"Resume Acknowledge" },
  { Q931_MT_RESUME_REJECT,		"Resume Reject" },
  { Q931_MT_SUSPEND,			"Suspend" },
  { Q931_MT_SUSPEND_ACKNOWLEDGE,	"Suspend Acknowledge" },
  { Q931_MT_SUSPEND_REJECT,		"Suspend Reject" },
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
         sizeof(q931_message_type_names)/
           sizeof(struct q931_message_type_name),
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
       sizeof(q931_message_type_names)/
         sizeof(struct q931_message_type_name),
       sizeof(struct q931_message_type_name),
       q931_message_type_id_compare);
}

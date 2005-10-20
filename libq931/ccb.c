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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/ccb.h>

void q931_ccb_dispatch(struct q931_ccb_message *msg)
{
	switch (msg->primitive) {
	case Q931_CCB_ALERTING_REQUEST:
		q931_alerting_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_DISCONNECT_REQUEST:
		q931_disconnect_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_INFO_REQUEST:
		q931_info_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_MORE_INFO_REQUEST:
		q931_more_info_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_NOTIFY_REQUEST:
		q931_notify_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_PROCEEDING_REQUEST:
		q931_proceeding_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_PROGRESS_REQUEST:
		q931_progress_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_REJECT_REQUEST:
		q931_reject_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_RELEASE_REQUEST:
		q931_release_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_RESUME_REQUEST:
		q931_resume_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_RESUME_REJECT_REQUEST:
		q931_resume_reject_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_RESUME_RESPONSE:
		q931_resume_response(msg->call, &msg->ies);
	break;

	case Q931_CCB_SETUP_COMPLETE_REQUEST:
		q931_setup_complete_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_SETUP_REQUEST:
		q931_setup_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_SETUP_RESPONSE:
		q931_setup_response(msg->call, &msg->ies);
	break;

	case Q931_CCB_STATUS_ENQUIRY_REQUEST:
		q931_status_enquiry_request(msg->call,
			(struct q931_ces *)msg->par1,
			&msg->ies);
	break;

	case Q931_CCB_SUSPEND_REJECT_REQUEST:
		q931_suspend_reject_request(msg->call, &msg->ies);
	break;

	case Q931_CCB_SUSPEND_RESPONSE:
		q931_suspend_response(msg->call, &msg->ies);
	break;

	case Q931_CCB_SUSPEND_REQUEST:
		q931_suspend_request(msg->call, &msg->ies);
	break;

	default:;
		// Unexpected primitive
	}
}

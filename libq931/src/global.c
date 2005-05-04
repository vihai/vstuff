#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define Q931_PRIVATE

#include <assert.h>

#include "lib.h"
#include "intf.h"
#include "logging.h"
#include "global.h"
#include "out.h"
#include "chanset.h"

#include "ie_chanid.h"
#include "ie_call_state.h"

static const char *q931_global_state_to_text(enum q931_global_state state)
{
	switch (state) {
	case Q931_GLOBAL_STATE_NULL:
		return "NULL";
	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		return "RESTART REQUEST";
	case Q931_GLOBAL_STATE_RESTART:
		return "RESTART";
	}

	return NULL;
}

void q931_global_set_state(
	struct q931_global_call *gc,
	enum q931_global_state state)
{
	assert(gc);

	report_intf(gc->intf, LOG_DEBUG,
		"Global call changed state from %s to %s\n",
		q931_global_state_to_text(gc->state),
		q931_global_state_to_text(state));

	gc->state = state;
}

void q931_management_restart_request(
	struct q931_global_call *gc,
	struct q931_chanset *chanset)
{
	if (gc->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT &&
	    gc->intf->role == LAPD_ROLE_NT) {
		report_intf(gc->intf, LOG_ERR,
			"Cannot start restart procedure on a"
			" multipoint network interface\n");

		return;
	}

	switch(gc->state) {
	case Q931_GLOBAL_STATE_NULL: {

		gc->T317_expired = FALSE;
		gc->restart_retransmit_count = 0;
		gc->restart_responded = FALSE;
		gc->restart_acknowledged = 0;
		gc->restart_request_count = 0;

		q931_chanset_init(&gc->restart_acked_chans);
		q931_chanset_copy(&gc->restart_reqd_chans, chanset);

		q931_send_restart(gc, &gc->intf->dlc, chanset);
		q931_global_start_timer(gc, T316);

		struct q931_call *call;
		list_for_each_entry(call, &gc->intf->calls, calls_node) {
			if (q931_chanset_contains(chanset, call->channel->id)) {
				q931_restart_request(call);
				gc->restart_request_count++;

				if (!q931_global_timer_running(gc, T317))
					q931_global_start_timer(gc, T317);
			} else {
				gc->restart_request_count++;
				q931_global_restart_confirm(gc, call);
			}
		}

		q931_global_set_state(gc, Q931_GLOBAL_STATE_RESTART_REQUEST);
	}
	break;

	case Q931_GLOBAL_STATE_RESTART_REQUEST:
	break;

	case Q931_GLOBAL_STATE_RESTART:
	break;
	}
}

void q931_global_restart_confirm(
	struct q931_global_call *gc,
	struct q931_call *call)
{
	switch(gc->state) {
	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		gc->restart_request_count--;

		if (gc->restart_request_count == 0) {
			q931_global_stop_timer(gc, T317);

			if (gc->restart_acknowledged) {
				struct q931_chanset cs;
				q931_chanset_init(&cs);

				q931_chanset_intersect(
					&gc->restart_reqd_chans,
					&gc->restart_acked_chans);

				q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);

				if (gc->intf->management_restart_confirm)
					gc->intf->management_restart_confirm(gc, &cs);
			} else {
				gc->restart_responded = TRUE;
			}
		} else {
			q931_chanset_add(&gc->restart_acked_chans,
				call->channel->id);
		}
	break;

	case Q931_GLOBAL_STATE_RESTART:
		gc->restart_request_count--;

		if (gc->restart_request_count == 0) {
			q931_global_stop_timer(gc, T317);
			q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);
			q931_send_restart_acknowledge(gc, &gc->intf->dlc,
				&gc->restart_acked_chans);
		} else {
			q931_chanset_add(&gc->restart_acked_chans,
				call->channel->id);
		}
	break;

	case Q931_GLOBAL_STATE_NULL:
		// Unexpected primitive
	break;
	}
}

static __u8 q931_global_state_to_ie_state(enum q931_global_state state)
{
        switch (state) {
	case Q931_GLOBAL_STATE_NULL:
		return Q931_IE_CS_REST0;
	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		return Q931_IE_CS_REST1;
	case Q931_GLOBAL_STATE_RESTART:
		return Q931_IE_CS_REST2;
	}

	return 0;
}

inline static void q931_global_handle_status(
	struct q931_global_call *gc,
	struct q931_message *msg)
{
	switch(gc->state) {
	case Q931_GLOBAL_STATE_NULL:
		// Do nothing
	break;

	case Q931_GLOBAL_STATE_RESTART_REQUEST:
	case Q931_GLOBAL_STATE_RESTART: {
		__u8 onwire_state = 0;

		int i;
		for(i=0; i<msg->ies_cnt; i++) {
			if (msg->ies[i].info->id == Q931_IE_CALL_STATE) {
				struct q931_ie_call_state_onwire_3 *oct_3 =
					(struct q931_ie_call_state_onwire_3 *)
					(msg->ies[i].data + 0);

				onwire_state = oct_3->value;

				break;
			}
		}

		if (onwire_state == q931_global_state_to_ie_state(gc->state)) {
			// Implementation dependent
		} else {
			if (gc->intf->status_management_indication)
				gc->intf->status_management_indication(gc); // With error
		}
	}
	break;
	}
}

inline static void q931_global_handle_restart(
	struct q931_global_call *gc,
	struct q931_message *msg)
{
	switch(gc->state) {
	case Q931_GLOBAL_STATE_NULL:
		gc->T317_expired = FALSE;
		gc->restart_retransmit_count = 0;
		gc->restart_responded = FALSE;
		gc->restart_acknowledged = 0;
		gc->restart_request_count = 0;

		struct q931_chanset cs;
		q931_chanset_init(&cs);

		int i;
		for (i=0; i<msg->ies_cnt; i++) {
			if (msg->ies[i].info->id == Q931_IE_CHANNEL_IDENTIFICATION)
				q931_ie_channel_identification_to_chanset(&msg->ies[i], &cs);
		}

		// Any indicated channel allowed to be restarted?
		if (1) {
			struct q931_call *call;
			list_for_each_entry(call, &gc->intf->calls, calls_node) {
				if (q931_chanset_contains(&cs, call->channel->id)) {

					gc->restart_request_count++;
					q931_restart_request(call);

					if (!q931_global_timer_running(gc, T317))
						q931_global_start_timer(gc, T317);
				} else {
					gc->restart_request_count++;
					q931_global_restart_confirm(gc, call);
				}
			}

			q931_global_set_state(gc, Q931_GLOBAL_STATE_RESTART);
		} else {
			struct q931_causeset causeset = Q931_CAUSESET_INITC(
                                Q931_IE_C_CV_IDENTIFIED_CHANNEL_DOES_NOT_EXIST);

			q931_global_send_status(gc, msg->dlc, &causeset);
		}
	break;

	case Q931_GLOBAL_STATE_RESTART_REQUEST:
	case Q931_GLOBAL_STATE_RESTART: {
		struct q931_causeset causeset = Q931_CAUSESET_INITC(
                        Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);

		q931_global_send_status(gc, msg->dlc, &causeset);
	}
	break;
	}
}

inline static void q931_global_handle_restart_acknowledge(
	struct q931_global_call *gc,
	struct q931_message *msg)
{
	switch(gc->state) {
	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		q931_global_stop_timer(gc, T316);

		if (gc->restart_responded || gc->T317_expired) {

			struct q931_chanset cs;
			q931_chanset_init(&cs);

			q931_chanset_intersect(
				&gc->restart_reqd_chans,
				&gc->restart_acked_chans);

			if (gc->intf->management_restart_confirm)
				gc->intf->management_restart_confirm(gc, &cs);

			q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);
		} else if (!gc->T317_expired) {

			// Store channel id received in restart ack
			int i;
			for (i=0; i<msg->ies_cnt; i++) {
				if (msg->ies[i].info->id == Q931_IE_CHANNEL_IDENTIFICATION) {

					struct q931_chanset cs;

					q931_ie_channel_identification_to_chanset(
						&msg->ies[i],
						&cs);

					q931_chanset_merge(&gc->restart_acked_chans, &cs);

					break;
				}
			}

			gc->restart_acknowledged = TRUE;
		}
	break;

	case Q931_GLOBAL_STATE_NULL:
	case Q931_GLOBAL_STATE_RESTART: {
		struct q931_causeset causeset = Q931_CAUSESET_INITC(
                        Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);

		q931_global_send_status(gc, msg->dlc, &causeset);
	}
	break;
	}
}

void q931_global_timer_T316(void *data)
{
	struct q931_global_call *gc = data;
	assert(data);

	switch(gc->state) {
	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		gc->restart_retransmit_count++;

		if (gc->restart_retransmit_count >= 2) {
			q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);

			// Indicate that restart has failed
			if (gc->intf->management_restart_confirm)
				gc->intf->management_restart_confirm(gc, NULL);
		} else {
			q931_send_restart(gc, &gc->intf->dlc, &gc->restart_reqd_chans);
			q931_global_start_timer(gc, T316);
		}
	break;

	case Q931_GLOBAL_STATE_NULL:
	case Q931_GLOBAL_STATE_RESTART:
		// Unexpected timer
	break;
	}
}

void q931_global_timer_T317(void *data)
{
	struct q931_global_call *gc = data;
	assert(data);

	switch(gc->state) {
	case Q931_GLOBAL_STATE_NULL:
		// Unexpected timer
	break;

	case Q931_GLOBAL_STATE_RESTART_REQUEST:
		if (gc->restart_acknowledged) {
			struct q931_chanset cs;
			q931_chanset_init(&cs);

			q931_chanset_intersect(
				&gc->restart_reqd_chans,
				&gc->restart_acked_chans);

			q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);

			if (gc->intf->management_restart_confirm)
				gc->intf->management_restart_confirm(gc, &cs);
		} else {
			gc->T317_expired = TRUE;
		}
	break;

	case Q931_GLOBAL_STATE_RESTART:
		// Any channel restarted?
		if (q931_chanset_count(&gc->restart_acked_chans)) {
			q931_send_restart_acknowledge(gc, &gc->intf->dlc,
				&gc->restart_acked_chans);
		}

		q931_global_set_state(gc, Q931_GLOBAL_STATE_NULL);

		if (gc->intf->timeout_management_indication)
			gc->intf->timeout_management_indication(gc);
	break;
	}
}

void q931_dispatch_global_message(
	struct q931_global_call *gc,
	struct q931_message *msg)
{
	switch(msg->message_type) {
	case Q931_MT_STATUS:
		q931_global_handle_status(gc, msg);
	break;

	case Q931_MT_RESTART:
		q931_global_handle_restart(gc, msg);
	break;

	case Q931_MT_RESTART_ACKNOWLEDGE:
		q931_global_handle_restart_acknowledge(gc, msg);
	break;

	default: {
		struct q931_causeset causeset = Q931_CAUSESET_INITC(
                        Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);

		q931_global_send_status(gc, msg->dlc, &causeset);
	}
	break;
	}
}


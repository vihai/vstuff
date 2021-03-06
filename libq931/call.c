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
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>

#include <linux/lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/ces.h>
#include <libq931/channel.h>
#include <libq931/proto.h>
#include <libq931/input.h>
#include <libq931/output.h>

#include <libq931/ie_sending_complete.h>
#include <libq931/ie_bearer_capability.h>
#include <libq931/ie_called_party_number.h>
#include <libq931/ie_calling_party_number.h>
#include <libq931/ie_channel_identification.h>
#include <libq931/ie_call_identity.h>
#include <libq931/ie_progress_indicator.h>
#include <libq931/ie_cause.h>
#include <libq931/ie_call_state.h>
#include <libq931/ie_high_layer_compatibility.h>
#include <libq931/ie_datetime.h>

#include "call_inline.h"

static void q931_timer_T301(void *data);
static void q931_timer_T302(void *data);
static void q931_timer_T303(void *data);
static void q931_timer_T304(void *data);
static void q931_timer_T305(void *data);
static void q931_timer_T306(void *data);
static void q931_timer_T308(void *data);
static void q931_timer_T309(void *data);
static void q931_timer_T310(void *data);
static void q931_timer_T312(void *data);
static void q931_timer_T313(void *data);
static void q931_timer_T314(void *data);
static void q931_timer_T316(void *data);
static void q931_timer_T318(void *data);
static void q931_timer_T319(void *data);
static void q931_timer_T320(void *data);
static void q931_timer_T321(void *data);
static void q931_timer_T322(void *data);

const char *q931_call_state_to_text(enum q931_call_state state)
{
	switch (state) {
	case U0_NULL_STATE:
		return "U0_NULL_STATE";
	case U1_CALL_INITIATED:
		return "U1_CALL_INITIATED";
	case U2_OVERLAP_SENDING:
		return "U2_OVERLAP_SENDING";
	case U3_OUTGOING_CALL_PROCEEDING:
		return "U3_OUTGOING_CALL_PROCEEDING";
	case U4_CALL_DELIVERED:
		return "U4_CALL_DELIVERED";
	case U6_CALL_PRESENT:
		return "U6_CALL_PRESENT";
	case U7_CALL_RECEIVED:
		return "U7_CALL_RECEIVED";
	case U8_CONNECT_REQUEST:
		return "U8_CONNECT_REQUEST";
	case U9_INCOMING_CALL_PROCEEDING:
		return "U9_INCOMING_CALL_PROCEEDING";
	case U10_ACTIVE:
		return "U10_ACTIVE";
	case U11_DISCONNECT_REQUEST:
		return "U11_DISCONNECT_REQUEST";
	case U12_DISCONNECT_INDICATION:
		return "U12_DISCONNECT_INDICATION";
	case U15_SUSPEND_REQUEST:
		return "U15_SUSPEND_REQUEST";
	case U17_RESUME_REQUEST:
		return "U17_RESUME_REQUEST";
	case U19_RELEASE_REQUEST:
		return "U19_RELEASE_REQUEST";
	case U25_OVERLAP_RECEIVING:
		return "U25_OVERLAP_RECEIVING";
	case N0_NULL_STATE:
		return "N0_NULL_STATE";
	case N1_CALL_INITIATED:
		return "N1_CALL_INITIATED";
	case N2_OVERLAP_SENDING:
		return "N2_OVERLAP_SENDING";
	case N3_OUTGOING_CALL_PROCEEDING:
		return "N3_OUTGOING_CALL_PROCEEDING";
	case N4_CALL_DELIVERED:
		return "N4_CALL_DELIVERED";
	case N6_CALL_PRESENT:
		return "N6_CALL_PRESENT";
	case N7_CALL_RECEIVED:
		return "N7_CALL_RECEIVED";
	case N8_CONNECT_REQUEST:
		return "N8_CONNECT_REQUEST";
	case N9_INCOMING_CALL_PROCEEDING:
		return "N9_INCOMING_CALL_PROCEEDING";
	case N10_ACTIVE:
		return "N10_ACTIVE";
	case N11_DISCONNECT_REQUEST:
		return "N11_DISCONNECT_REQUEST";
	case N12_DISCONNECT_INDICATION:
		return "N12_DISCONNECT_INDICATION";
	case N15_SUSPEND_REQUEST:
		return "N15_SUSPEND_REQUEST";
	case N17_RESUME_REQUEST:
		return "N17_RESUME_REQUEST";
	case N19_RELEASE_REQUEST:
		return "N19_RELEASE_REQUEST";
	case N22_CALL_ABORT:
		return "N22_CALL_ABORT";
	case N25_OVERLAP_RECEIVING:
		return "N25_OVERLAP_RECEIVING";
	default: return "*UNKNOWN*";
	}
}

void q931_call_set_state(
	struct q931_call *call,
	enum q931_call_state state)
{
	assert(call);

	report_call(call, LOG_DEBUG,
		"%s ==to==> %s\n",
		q931_call_state_to_text(call->state),
		q931_call_state_to_text(state));

	if ((call->state == U0_NULL_STATE ||
	     call->state == U1_CALL_INITIATED ||
	     call->state == U2_OVERLAP_SENDING ||
	     call->state == U3_OUTGOING_CALL_PROCEEDING ||
	     call->state == U4_CALL_DELIVERED ||
	     call->state == U6_CALL_PRESENT ||
	     call->state == U7_CALL_RECEIVED ||
	     call->state == U8_CONNECT_REQUEST ||
	     call->state == U9_INCOMING_CALL_PROCEEDING ||
	     call->state == U10_ACTIVE ||
	     call->state == U11_DISCONNECT_REQUEST ||
	     call->state == U12_DISCONNECT_INDICATION ||
	     call->state == U15_SUSPEND_REQUEST ||
	     call->state == U17_RESUME_REQUEST ||
	     call->state == U19_RELEASE_REQUEST ||
	     call->state == U25_OVERLAP_RECEIVING) &&
	    (call->state == N0_NULL_STATE ||
	     call->state == N1_CALL_INITIATED ||
	     call->state == N2_OVERLAP_SENDING ||
	     call->state == N3_OUTGOING_CALL_PROCEEDING ||
	     call->state == N4_CALL_DELIVERED ||
	     call->state == N6_CALL_PRESENT ||
	     call->state == N7_CALL_RECEIVED ||
	     call->state == N8_CONNECT_REQUEST ||
	     call->state == N9_INCOMING_CALL_PROCEEDING ||
	     call->state == N10_ACTIVE ||
	     call->state == N11_DISCONNECT_REQUEST ||
	     call->state == N12_DISCONNECT_INDICATION ||
	     call->state == N15_SUSPEND_REQUEST ||
	     call->state == N17_RESUME_REQUEST ||
	     call->state == N19_RELEASE_REQUEST ||
	     call->state == N22_CALL_ABORT ||
	     call->state == N25_OVERLAP_RECEIVING)) {
		report_call(call, LOG_ERR,
			"AAAAAHRG!!! CHANGING BETWEEN USER/NET STATE!!!!!\n");
	}

	call->state = state;

}

struct q931_call *q931_call_alloc(struct q931_interface *intf)
{
	struct q931_call *call;

	call = malloc(sizeof(*call));
	if (!call)
		return NULL;

	memset(call, 0, sizeof(*call));

	call->refcnt = 1;

	call->intf = intf;

	if (call->intf->role == LAPD_INTF_ROLE_TE)
		call->state = U0_NULL_STATE;
	else
		call->state = N0_NULL_STATE;

	INIT_LIST_HEAD(&call->ces);

	q931_ies_init(&call->setup_ies);

	q931_ies_init(&call->disconnect_cause);
	q931_ies_init(&call->saved_cause);
	q931_ies_init(&call->release_cause);

	q931_init_timer(&call->T301, "T301", q931_timer_T301, call);
	q931_init_timer(&call->T302, "T302", q931_timer_T302, call);
	q931_init_timer(&call->T303, "T303", q931_timer_T303, call);
	q931_init_timer(&call->T304, "T304", q931_timer_T304, call);
	q931_init_timer(&call->T305, "T305", q931_timer_T305, call);
	q931_init_timer(&call->T306, "T306", q931_timer_T306, call);
	q931_init_timer(&call->T308, "T308", q931_timer_T308, call);
	q931_init_timer(&call->T309, "T309", q931_timer_T309, call);
	q931_init_timer(&call->T310, "T310", q931_timer_T310, call);
	q931_init_timer(&call->T312, "T312", q931_timer_T312, call);
	q931_init_timer(&call->T313, "T313", q931_timer_T313, call);
	q931_init_timer(&call->T314, "T314", q931_timer_T314, call);
	q931_init_timer(&call->T316, "T316", q931_timer_T316, call);
	q931_init_timer(&call->T318, "T318", q931_timer_T318, call);
	q931_init_timer(&call->T319, "T319", q931_timer_T319, call);
	q931_init_timer(&call->T320, "T320", q931_timer_T320, call);
	q931_init_timer(&call->T321, "T321", q931_timer_T321, call);
	q931_init_timer(&call->T322, "T322", q931_timer_T322, call);

	return call;
}

struct q931_call *q931_call_alloc_in(
	struct q931_interface *intf,
	struct q931_dlc *dlc,
	int call_reference)
{
	assert(intf);
	assert(dlc);

	struct q931_call *call;

	call = q931_call_alloc(intf);
	if (!call)
		return NULL;

	if (dlc->tei == LAPD_BROADCAST_TEI) {
		call->dlc = q931_dlc_get(&dlc->intf->dlc);
		q931_dlc_hold(&dlc->intf->dlc);
		call->broadcast_setup = TRUE;
	} else {
		call->dlc = q931_dlc_get(dlc);
		q931_dlc_hold(dlc);
		call->broadcast_setup = FALSE;
	}

	call->direction = Q931_CALL_DIRECTION_INBOUND;
	call->call_reference = call_reference;

	q931_intf_add_call(intf, q931_call_get(call));

	return call;
}

struct q931_call *q931_call_alloc_out(
	struct q931_interface *intf)
{
	assert(intf);

	struct q931_call *call;
	call = q931_call_alloc(intf);
	if (!call)
		goto err_call_alloc;

	call->direction = Q931_CALL_DIRECTION_OUTBOUND;
	call->call_reference =
		q931_intf_find_free_call_reference(call->intf);

	if (call->call_reference < 0) {
		report_intf(intf, LOG_ERR,
			"Cannot find an available call reference number!\n");

		goto err_find_callref;
	}

	if (intf->role == LAPD_INTF_ROLE_TE ||
	    intf->mode == LAPD_INTF_MODE_POINT_TO_POINT) {
		call->dlc = q931_dlc_get(&intf->dlc);
		q931_dlc_hold(call->dlc);
	} else {
		call->dlc = NULL;
	}

	q931_intf_add_call(intf, q931_call_get(call));

	return call;

err_find_callref:
	free(call);
err_call_alloc:

	return NULL;
}

struct q931_call *_q931_call_get(struct q931_call *call,
	const char *file,
	int line)
{
	assert(call);
	assert(call->refcnt > 0);

#if 1
	report_call(call, LOG_DEBUG, "%s:%d GET (%d => %d)\n",
		file, line,
		call->refcnt, call->refcnt+1);
#endif

	call->refcnt++;

	return call;
}

void _q931_call_put(struct q931_call *call,
	const char *file,
	int line)
{
	assert(call);
	assert(call->refcnt > 0);

#if 1
	report_call(call, LOG_DEBUG, "%s:%d PUT (%d => %d)\n",
		file, line,
		call->refcnt, call->refcnt-1);
#endif

	call->refcnt--;

	if (!call->refcnt) {
		report_call(call, LOG_DEBUG, "Freeing call\n");

		if (call->dlc) {
			q931_dlc_release(call->dlc);
			q931_dlc_put(call->dlc);
		}

		free(call);
	}
}

void q931_call_release_reference(
	struct q931_call *call)
{
	q931_intf_del_call(call);
	q931_call_put(call);
}

void _q931_call_restart_timer(
	struct q931_call *call,
	struct q931_timer *timer,
	int delta)
{
	if (!q931_timer_pending(timer))
		q931_call_get(call);

	q931_start_timer_delta(timer, delta);
}

void _q931_call_start_timer(
	struct q931_call *call,
	struct q931_timer *timer,
	int delta)
{
	if (!q931_timer_pending(timer)) {
		q931_call_get(call);

		q931_start_timer_delta(timer, delta);
	}
}

void _q931_call_stop_timer(
	struct q931_call *call,
	struct q931_timer *timer)
{
	if (q931_timer_pending(timer)) {
		q931_stop_timer(timer);
		q931_call_put(call);
	}
}

struct q931_call *q931_get_call_by_reference(
	struct q931_interface *intf,
	enum q931_call_direction direction,
	q931_callref call_reference)
{
	assert(intf);

	struct q931_call *call;
	list_for_each_entry(call, &intf->calls, calls_node) {

		if (call->direction == direction &&
		    call->call_reference == call_reference) {
			return q931_call_get(call);
		}
	}

	return NULL;
}

void q931_call_stop_any_timer(struct q931_call *call)
{
	assert(call);

	q931_call_stop_timer(call, T301);
	q931_call_stop_timer(call, T302);
	q931_call_stop_timer(call, T303);
	q931_call_stop_timer(call, T304);
	q931_call_stop_timer(call, T305);
	q931_call_stop_timer(call, T306);
	q931_call_stop_timer(call, T308);
	q931_call_stop_timer(call, T309);
	q931_call_stop_timer(call, T310);
	q931_call_stop_timer(call, T312);
	q931_call_stop_timer(call, T313);
	q931_call_stop_timer(call, T314);
	q931_call_stop_timer(call, T316);
	q931_call_stop_timer(call, T318);
	q931_call_stop_timer(call, T319);
	q931_call_stop_timer(call, T320);
	q931_call_stop_timer(call, T321);
	q931_call_stop_timer(call, T322);
}

static __u8 q931_call_state_to_ie_state(enum q931_call_state state)
{
	switch (state) {
	case N0_NULL_STATE:
		return Q931_IE_CS_N0_NULL_STATE;
	case N1_CALL_INITIATED:
		return Q931_IE_CS_N1_CALL_INITIATED;
	case N2_OVERLAP_SENDING:
		return Q931_IE_CS_N2_OVERLAP_SENDING;
	case N3_OUTGOING_CALL_PROCEEDING:
		return Q931_IE_CS_N3_OUTGOING_CALL_PROCEEDING;
	case N4_CALL_DELIVERED:
		return Q931_IE_CS_N4_CALL_DELIVERED;
	case N6_CALL_PRESENT:
		return Q931_IE_CS_N6_CALL_PRESENT;
	case N7_CALL_RECEIVED:
		return Q931_IE_CS_N7_CALL_RECEIVED;
	case N8_CONNECT_REQUEST:
		return Q931_IE_CS_N8_CONNECT_REQUEST;
	case N9_INCOMING_CALL_PROCEEDING:
		return Q931_IE_CS_N9_INCOMING_CALL_PROCEEDING;
	case N10_ACTIVE:
		return Q931_IE_CS_N10_ACTIVE;
	case N11_DISCONNECT_REQUEST:
		return Q931_IE_CS_N11_DISCONNECT_REQUEST;
	case N12_DISCONNECT_INDICATION:
		return Q931_IE_CS_N12_DISCONNECT_INDICATION;
	case N15_SUSPEND_REQUEST:
		return Q931_IE_CS_N15_SUSPEND_REQUEST;
	case N17_RESUME_REQUEST:
		return Q931_IE_CS_N17_RESUME_REQUEST;
	case N19_RELEASE_REQUEST:
		return Q931_IE_CS_N19_RELEASE_REQUEST;
	case N22_CALL_ABORT:
		return Q931_IE_CS_N22_CALL_ABORT;
	case N25_OVERLAP_RECEIVING:
		return Q931_IE_CS_N25_OVERLAP_RECEIVING;
	case U0_NULL_STATE:
		return Q931_IE_CS_U0_NULL_STATE;
	case U1_CALL_INITIATED:
		return Q931_IE_CS_U1_CALL_INITIATED;
	case U2_OVERLAP_SENDING:
		return Q931_IE_CS_U2_OVERLAP_SENDING;
	case U3_OUTGOING_CALL_PROCEEDING:
		return Q931_IE_CS_U3_OUTGOING_CALL_PROCEEDING;
	case U4_CALL_DELIVERED:
		return Q931_IE_CS_U4_CALL_DELIVERED;
	case U6_CALL_PRESENT:
		return Q931_IE_CS_U6_CALL_PRESENT;
	case U7_CALL_RECEIVED:
		return Q931_IE_CS_U7_CALL_RECEIVED;
	case U8_CONNECT_REQUEST:
		return Q931_IE_CS_U8_CONNECT_REQUEST;
	case U9_INCOMING_CALL_PROCEEDING:
		return Q931_IE_CS_U9_INCOMING_CALL_PROCEEDING;
	case U10_ACTIVE:
		return Q931_IE_CS_U10_ACTIVE;
	case U11_DISCONNECT_REQUEST:
		return Q931_IE_CS_U11_DISCONNECT_REQUEST;
	case U12_DISCONNECT_INDICATION:
		return Q931_IE_CS_U12_DISCONNECT_INDICATION;
	case U15_SUSPEND_REQUEST:
		return Q931_IE_CS_U15_SUSPEND_REQUEST;
	case U17_RESUME_REQUEST:
		return Q931_IE_CS_U17_RESUME_REQUEST;
	case U19_RELEASE_REQUEST:
		return Q931_IE_CS_U19_RELEASE_REQUEST;
	case U25_OVERLAP_RECEIVING:
		return Q931_IE_CS_U25_OVERLAP_RECEIVING;
	}

	return 0;
}

static void q931_call_send_call_status_with_state(
	struct q931_call *call,
	struct q931_ies *user_ies,
	enum q931_call_state state)
{
	Q931_DECLARE_IES(ies);

	q931_ies_copy(&ies, user_ies);

	struct q931_ie_call_state *cs = q931_ie_call_state_alloc();
	cs->coding_standard = Q931_IE_CS_CS_CCITT;
	cs->value = q931_call_state_to_ie_state(state);
	q931_ies_add_put(&ies, &cs->ie);

	q931_call_send_status(call, &ies);
	Q931_UNDECLARE_IES(ies);
}

static inline void q931_call_send_call_status(
	struct q931_call *call,
	struct q931_ies *user_ies)
{
	q931_call_send_call_status_with_state(call, user_ies, call->state);
}

#define q931_call_unexpected_primitive(call)	\
	_q931_call_unexpected_primitive((call), __FUNCTION__)

void _q931_call_unexpected_primitive(
	struct q931_call *call,
	const char *event)
{
	assert(call);
	assert(event);

	report_call(call, LOG_WARNING,
		"Unexpected %s in state %s\n",
		event,
		q931_call_state_to_text(call->state));
}

#define q931_call_unexpected_timer(call)	\
	_q931_call_unexpected_timer((call), __FUNCTION__)

void _q931_call_unexpected_timer(
	struct q931_call *call,
	const char *event)
{
	assert(call);
	assert(event);

	report_call(call, LOG_WARNING,
		"Unexpected %s in state %s\n",
		event,
		q931_call_state_to_text(call->state));
}

#define q931_call_message_not_compatible_with_state_n6(call, msg)	\
	_q931_call_message_not_compatible_with_state(	\
		(call), (msg), __FUNCTION__, N6_CALL_PRESENT)

#define q931_call_message_not_compatible_with_state(call, msg)	\
	_q931_call_message_not_compatible_with_state(		\
		(call), (msg), __FUNCTION__, -1)

void _q931_call_message_not_compatible_with_state(
	struct q931_call *call,
	const struct q931_message *msg,
	const char *event,
	enum q931_call_state new_state)
{
	assert(call);
	assert(event);

	report_call(call, LOG_WARNING,
		"Unexpected %s in state %s\n",
		event,
		q931_call_state_to_text(call->state));

	Q931_DECLARE_IES(ies);
	struct q931_ie_cause *cause = q931_ie_cause_alloc();

	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(call);

	if (call->state == N0_NULL_STATE) {
		cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_release(call, &ies);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	} if (call->state == U0_NULL_STATE) {
		cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_release(call, &ies);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	} else {
		cause->value =
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;

		memcpy(cause->diagnostics, &msg->raw_message_type,
			sizeof(msg->raw_message_type));
		cause->diagnostics_len = sizeof(msg->raw_message_type);

		q931_ies_add_put(&ies, &cause->ie);

		if (new_state == -1)
			q931_call_send_call_status(call, &ies);
		else
			q931_call_send_call_status_with_state(call, &ies,
						new_state);
	}

	Q931_UNDECLARE_IES(ies);
}

static int q931_channel_select_response(
	struct q931_call *call,
	const struct q931_ies *ies,
	struct q931_ies *causes)
{
	assert(call);
	assert(ies);
	assert(call->intf);

	struct q931_ie_channel_identification *ci = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CHANNEL_IDENTIFICATION) {
			ci = container_of(ies->ies[i],
				struct q931_ie_channel_identification, ie);

			break;
		}
	}

	if (!ci) {
		// No channel identification IE
		if (!call->channel) {
			report_call(call, LOG_DEBUG,
				"No channel identification IE and no proposed"
				" channel\n");

			return FALSE;
		} else {
			report_call(call, LOG_DEBUG,
				"No channel identification IE, using proposed"
				" channel %d\n",
				call->channel->id);

			call->channel = call->channel;
			call->channel->call = call;

			return TRUE;
		}
	}

	if (!call->channel) {
		// Ok, we did not indicate a channel so, attempt to use what
		// other party requests in his response

		int i;
		for (i=0; ci->chanset.nchans; i++) {
			if (ci->chanset.chans[i]->state ==
					Q931_CHANSTATE_AVAILABLE) {
				// Nice, the channel is available

				report_call(call, LOG_DEBUG,
					"No channel proposed in setup, "
					"using indicated channel B%d\n",
					ci->chanset.chans[i]->id+1);

				call->channel = ci->chanset.chans[i];
				call->channel->call = call;

				q931_channel_set_state(call->channel,
						Q931_CHANSTATE_SELECTED);

				return TRUE;
			} else {
				report_call(call, LOG_DEBUG,
					"No channel proposed in setup, "
					"but indicated channel B%d is "
					"unavailable\n",
					ci->chanset.chans[i]->id+1);
			}

			report_call(call, LOG_DEBUG,
				"No channel proposed in setup, "
				"but no indicated channel is unavailable\n");
		}

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_CHANNEL_UNACCEPTABLE;
		q931_ies_add_put(causes, &cause->ie);

		return FALSE;
	}

	// Uh oh, we already indicated the same channel
	if (q931_chanset_contains(&ci->chanset, call->channel)) {
		// Good, the channel is compatible, we will use it

		if (ci->preferred_exclusive == Q931_IE_CI_PE_PREFERRED) {
			// The other party shoudn't have responded
			// with acceptable alternatives but we are
			// tolerant.

			report_call(call, LOG_WARNING,
				"Party responded with channel"
				" alternatives, this is out of"
				" standard, but tolerable\n");
		}

		return TRUE;
	} else {
		/* Uh, well, we proposed another channel but
		 * the other party doesn't agree. We are nice
		 * and use her choice.
		 */

		int i;
		for (i=0; ci->chanset.nchans; i++) {
			if (ci->chanset.chans[i]->state ==
					Q931_CHANSTATE_AVAILABLE) {

				if (call->channel)
					q931_channel_release(call->channel);

				call->channel = ci->chanset.chans[i];
				call->channel->call = call;

				q931_channel_set_state(call->channel,
						Q931_CHANSTATE_SELECTED);

				return TRUE;
			}

		}

		// The other channel is not available too
		// We're out of luck

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value =
			Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE;
		q931_ies_add_put(causes, &cause->ie);

		return FALSE;
	}

	assert(0);

	return FALSE;
}

static struct q931_channel *q931_channel_select_setup(
	struct q931_call *call,
	const struct q931_ies *setup_ies,
	struct q931_ies *causes,
	int *bumping_needed)
{
	assert(call);
	assert(setup_ies);
	assert(call->intf);

	*bumping_needed = FALSE;

	struct q931_ie_channel_identification *ci = NULL;

	int i;
	for(i=0; i<setup_ies->count; i++) {
		if (setup_ies->ies[i]->cls->id ==
				Q931_IE_CHANNEL_IDENTIFICATION) {

			ci = container_of(setup_ies->ies[i],
				struct q931_ie_channel_identification, ie);

			break;
		}
	}

	if (!ci) {
		// No Channel Identification IE, assuming any channel is ok
		// Presence of CI n->u is already checked in q931_decode_ies
		report_call(call, LOG_DEBUG,
			"No channel identification IE,"
			" assuming any channel available\n");

		int i;
		for (i=0; i<call->intf->n_channels; i++) {
			if (call->intf->channels[i].state ==
					Q931_CHANSTATE_AVAILABLE) {
				return &call->intf->channels[i];
			}
		}

		report_call(call, LOG_DEBUG,
			"No channel identification IE,"
			" and no channel available\n");

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE;
		q931_ies_add_put(causes, &cause->ie);

		return NULL;
	}

	if (!ci->chanset.nchans) {
		*bumping_needed = TRUE;
		return NULL;
	}

	for (i=0; i<ci->chanset.nchans; i++) {
		if (ci->chanset.chans[i]->state == Q931_CHANSTATE_AVAILABLE) {
			report_call(call, LOG_DEBUG,
				"Requested channel B%d available\n",
				ci->chanset.chans[i]->id+1);

			return ci->chanset.chans[i];
		}

		// The party indicated an unavailable channel

		if (ci->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			// Uuops, the party offers no alternative

			report_call(call, LOG_DEBUG,
				"Requested channel B%d unavailable and no "
				"alternatives offered\n",
				ci->chanset.chans[i]->id+1);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE;
			q931_ies_add_put(causes, &cause->ie);

			return NULL;
		}
	}

	// Let's see any alternative is ok
	for (i=0; i<call->intf->n_channels; i++) {
		if (call->intf->channels[i].state ==
				Q931_CHANSTATE_AVAILABLE) {

			report_call(call, LOG_DEBUG,
				"Requested channel unavailable but "
				"alternative B%d is ok\n",
				call->intf->channels[i].id+1);

			return &call->intf->channels[i];
		}
	}

	report_call(call, LOG_DEBUG, "No channel available\n");

	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(call);
	cause->value = Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE;
	q931_ies_add_put(causes, &cause->ie);

	return NULL;
}

void q931_alerting_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "ALERTING-REQ\n");

	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_send_alerting(call, user_ies);
		q931_call_set_state(call, N4_CALL_DELIVERED);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_call_send_alerting(call, user_ies);
		q931_call_set_state(call, N4_CALL_DELIVERED);
	break;

	case U6_CALL_PRESENT: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();

		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(
							call->intf);
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		q931_call_send_alerting(call, &ies);

		q931_call_set_state(call, U7_CALL_RECEIVED);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
		q931_call_send_alerting(call, user_ies);
		q931_call_set_state(call, U7_CALL_RECEIVED);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_send_alerting(call, user_ies);
		q931_call_set_state(call, U7_CALL_RECEIVED);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_disconnect_request(struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "DISCONNECT-REQ\n");

	call->disconnect_indication_sent = TRUE;

	Q931_DECLARE_IES(ies);
	q931_ies_copy(&ies, user_ies);

	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);

		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location =
				q931_ie_progress_indicator_location(call);
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_start_tone(call->channel,
				Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location =
				q931_ie_progress_indicator_location(call);
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_start_tone(call->channel,
				Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N6_CALL_PRESENT:
		q931_call_stop_timer(call, T303);

		if (call->broadcast_setup) {
			q931_channel_disconnect(call->channel);
			q931_call_set_state(call, N22_CALL_ABORT);
		} else {
			if (call->tones_option) {
				struct q931_ie_progress_indicator *pi =
					q931_ie_progress_indicator_alloc();
				pi->coding_standard = Q931_IE_PI_CS_CCITT;
				pi->location =
					q931_ie_progress_indicator_location(
									call);
				pi->progress_description =
					Q931_IE_PI_PD_IN_BAND_INFORMATION;
				q931_ies_add_put(&ies, &pi->ie);

				q931_call_send_disconnect(call, &ies);
				q931_channel_start_tone(call->channel,
					Q931_TONE_HANGUP);
				q931_call_start_timer(call, T306);
			} else {
				q931_channel_disconnect(call->channel);
				q931_call_send_disconnect(call, &ies);
				q931_call_start_timer(call, T305);
			}

			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N7_CALL_RECEIVED:
		q931_call_stop_timer(call, T301);

		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}
			} else {
				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}

				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					user_ies);

				/* If we do del call the CES will not be able
				   to receive release complete */

				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
			}
		} else {
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}

				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					NULL);
			}
		} else {
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);

		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}

				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					NULL);
			}
		} else {
			q931_call_send_disconnect(call, &ies);
			q931_channel_release(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N10_ACTIVE:
		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location =
				q931_ie_progress_indicator_location(call);
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_start_tone(call->channel,
				Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N15_SUSPEND_REQUEST:
		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location =
				q931_ie_progress_indicator_location(call);
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_start_tone(call->channel,
							 Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);

		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);

				struct q931_ces *ces;
				list_for_each_entry(ces, &call->ces, node) {
					q931_ces_release_request(ces, user_ies);
				}

				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					NULL);
			}
		} else {
			q931_call_send_disconnect(call, &ies);
			q931_channel_disconnect(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_channel_disconnect(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
		q931_channel_disconnect(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T313);
		q931_channel_disconnect(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_channel_disconnect(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}

	Q931_UNDECLARE_IES(ies);
}

void q931_info_request(
	struct q931_call *call,
	const struct q931_ies *ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "INFO-REQ\n");

	switch (call->state) {
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
		q931_call_send_information(call, ies);
	break;

	case N6_CALL_PRESENT:
		// Save event until completion of a transition???
		// FIXME TODO XXX
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_info_request(ces, ies);
			}
		} else {
			q931_call_send_information(call, ies);
		}
	break;

	case U1_CALL_INITIATED:
		// Do nothing
	break;

	case U2_OVERLAP_SENDING:
		q931_call_send_information(call, ies);

		if (q931_call_timer_running(call, T304))
			q931_call_restart_timer(call, T304);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U25_OVERLAP_RECEIVING:
		q931_call_send_information(call, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_more_info_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "MORE-INFO-REQ\n");

	switch (call->state) {
	case N1_CALL_INITIATED: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		q931_call_start_timer(call, T302);

		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();
		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(call->intf);
		ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location =
				q931_ie_progress_indicator_location(call);
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);
		}

		q931_call_send_setup_acknowledge(call, &ies);

		// Handle failed connection TODO
		q931_channel_connect(call->channel);

		if (!call->digits_dialled &&
		    call->tones_option) {
			q931_channel_start_tone(call->channel,
				Q931_TONE_DIAL);
		}

		q931_call_set_state(call, N2_OVERLAP_SENDING);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_info_request(ces, user_ies);
			}
		} else {
			q931_call_send_information(call, user_ies);
			q931_call_start_timer(call, T304);
		}
	break;

	case U6_CALL_PRESENT: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();
		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(
							call->intf);
		ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		q931_call_send_setup_acknowledge(call, &ies);

		q931_call_start_timer(call, T302);
		q931_call_set_state(call, U25_OVERLAP_RECEIVING);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_notify_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "NOTIFY-REQ\n");

	switch (call->state) {
	case N0_NULL_STATE:
		// Do nothing
	break;

	case N10_ACTIVE:
	case N15_SUSPEND_REQUEST:
		q931_call_send_notify(call, user_ies);
	break;

	case U10_ACTIVE:
		q931_call_send_notify(call, user_ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_proceeding_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "PROCEEDING-REQ\n");

	switch (call->state) {
	case N1_CALL_INITIATED: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();
		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(call->intf);
		ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		q931_call_send_call_proceeding(call, &ies);

		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_send_call_proceeding(call, user_ies);
		q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);
	break;

	case U6_CALL_PRESENT: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();
		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(
							call->intf);
		ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		q931_call_send_call_proceeding(call, &ies);

		q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_send_call_proceeding(call, user_ies);
		q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_progress_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "PROGRESS-REQ\n");

	switch (call->state) {
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
		q931_call_send_progress(call, user_ies);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_send_progress(call, user_ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_reject_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "REJECT-REQ\n");

	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_call_send_release_complete(call, user_ies);
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
	break;

	case U6_CALL_PRESENT:
		/* Release channel ?? */

		q931_call_send_release_complete(call, user_ies);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_release_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "RELEASE-REQ\n");

	switch (call->state) {
	case N11_DISCONNECT_REQUEST: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, user_ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_NORMAL_CALL_CLEARING;

		struct q931_ie_cause_diag_1_2 *diag =
			(struct q931_ie_cause_diag_1_2 *)cause->diagnostics;
		memset(diag, 0x00, sizeof(*diag));
		diag->ext1 = 0;
		diag->attribute_number = 0;
		diag->ext2 = 1;
		diag->condition = Q931_IE_C_D_C_PERMANENT;
		cause->diagnostics_len = sizeof(*diag);
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_release(call, &ies);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case U12_DISCONNECT_INDICATION: {
		Q931_DECLARE_IES(ies);

		q931_ies_merge(&ies, &call->disconnect_cause);
		q931_ies_merge(&ies, user_ies);

		q931_channel_disconnect(call->channel);
		q931_call_send_release(call, &ies);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "RESUME-REQ\n");

	if (call->intf->type != LAPD_INTF_TYPE_BRA)
		q931_call_unexpected_primitive(call);

	switch (call->state) {
	case U0_NULL_STATE:
		q931_call_send_resume(call, user_ies);
		q931_call_start_timer(call, T318);
		q931_call_set_state(call, U17_RESUME_REQUEST);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_reject_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "RESUME-REJECT-REQ\n");

	switch (call->state) {
	case N17_RESUME_REQUEST:
		// Ensure the lib's user specified IE cause

		q931_call_send_resume_reject(call, user_ies);
		q931_call_release_reference(call);
		q931_call_set_state(call, N0_NULL_STATE);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_response(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "RESUME-RESP\n");

	switch (call->state) {
	case N17_RESUME_REQUEST:
		q931_call_send_resume_acknowledge(call, user_ies);
		q931_call_set_state(call, N10_ACTIVE);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_setup_complete_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SETUP-COMPLETE-REQ\n");

	switch (call->state) {
	case N8_CONNECT_REQUEST:
		// Handle failed connection TODO
		q931_channel_connect(call->channel);

		if (call->broadcast_setup) {
			call->selected_ces = call->preselected_ces;
			call->dlc = q931_dlc_get(call->preselected_ces->dlc);
			q931_dlc_hold(call->preselected_ces->dlc);

			q931_call_send_connect_acknowledge(call, user_ies);
			q931_ces_free(call->selected_ces);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces = NULL;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, &ies);
			}

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_send_connect_acknowledge(call, user_ies);
		}

		q931_call_set_state(call, N10_ACTIVE);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

static void q931_channel_acquire(struct q931_call *call)
{
	assert(call);
	assert(call->intf);
	assert(!call->channel);

	int i;
	for(i=0; i<call->intf->n_channels; i++) {
		if (call->intf->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {

			struct q931_channel *chan =
				&call->intf->channels[i];

			chan->call = call;
			chan->call->channel = chan;

			q931_channel_set_state(call->channel,
					Q931_CHANSTATE_SELECTED);

			break;
		}
	}
}

void q931_setup_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SETUP-REQ\n");

	switch (call->state) {
	case N0_NULL_STATE: {

		q931_channel_acquire(call);

		if (call->channel || call->intf->enable_bumping) {

			q931_call_start_timer(call, T303);

			q931_ies_copy(&call->setup_ies, user_ies);

			struct q931_ie_channel_identification *ci =
				q931_ie_channel_identification_alloc();
			ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
			ci->interface_type =
				q931_ie_channel_identification_intftype(
								call->intf);
			ci->coding_standard = Q931_IE_CI_CS_CCITT;
			q931_chanset_init(&ci->chanset);

			if (call->channel)
				q931_chanset_add(&ci->chanset, call->channel);

			q931_ies_add_put(&call->setup_ies, &ci->ie);

			struct q931_ie_datetime *dt =
				q931_ie_datetime_alloc();
			dt->time = time(NULL);
			q931_ies_add_put(&call->setup_ies, &dt->ie);

			if (call->intf->mode == LAPD_INTF_MODE_MULTIPOINT) {

				ci->preferred_exclusive =
					Q931_IE_CI_PE_EXCLUSIVE;

				call->broadcast_setup = TRUE;
				q931_call_start_timer(call, T312);

				q931_call_send_setup_bc(call, &call->setup_ies);
			} else {
				ci->preferred_exclusive =
					Q931_IE_CI_PE_PREFERRED;

				q931_call_send_setup(call,
					&call->setup_ies);
			}

			q931_call_set_state(call, N6_CALL_PRESENT);

		} else {
			Q931_DECLARE_IES(ies);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &ies);
			Q931_UNDECLARE_IES(ies);
		}

	}
	break;

	case U0_NULL_STATE:
		q931_ies_copy(&call->setup_ies, user_ies);

		q931_call_send_setup(call, &call->setup_ies);
		q931_call_start_timer(call, T303);
		q931_call_set_state(call, U1_CALL_INITIATED);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_setup_response(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SETUP-RESP\n");

	Q931_DECLARE_IES(ies);

	q931_ies_merge(&ies, user_ies);

	switch (call->state) {
	case N2_OVERLAP_SENDING: {
		struct q931_ie_datetime *dt =
			q931_ie_datetime_alloc();
		dt->time = time(NULL);
		q931_ies_add_put(&ies, &dt->ie);

		q931_call_stop_timer(call, T302);
		q931_call_send_connect(call, &ies);
		q931_call_set_state(call, N10_ACTIVE);
	}
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED: {
		struct q931_ie_datetime *dt =
			q931_ie_datetime_alloc();
		dt->time = time(NULL);
		q931_ies_add_put(&ies, &dt->ie);

		q931_call_send_connect(call, &ies);
		q931_call_set_state(call, N10_ACTIVE);
	}
	break;

	case U6_CALL_PRESENT: {
		struct q931_ie_channel_identification *ci =
			q931_ie_channel_identification_alloc();
		ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
		ci->interface_type =
			q931_ie_channel_identification_intftype(
							call->intf);
		ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
		ci->coding_standard = Q931_IE_CI_CS_CCITT;
		q931_chanset_init(&ci->chanset);
		q931_chanset_add(&ci->chanset, call->channel);
		q931_ies_add_put(&ies, &ci->ie);

		q931_call_send_connect(call, &ies);

		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	}
	break;

	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
		q931_call_send_connect(call, user_ies);
		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_send_connect(call, user_ies);
		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}

	Q931_UNDECLARE_IES(ies);
}

void q931_status_enquiry_request(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "STATUS-ENQUIRY-REQ\n");

	switch (call->state) {
	case N0_NULL_STATE:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N19_RELEASE_REQUEST:
	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			if (call->state != N6_CALL_PRESENT) {
				if (ces) {
					q931_ces_status_enquiry_request(ces, user_ies);
				} else {
					struct q931_ces *ces;
					list_for_each_entry(ces, &call->ces, node) {
						q931_ces_status_enquiry_request(
							ces, user_ies);
					}
				}
			}
		} else {
			if (!q931_call_timer_running(call, T322)) {
				q931_call_send_status_enquiry(call, user_ies);
				call->senq_status_97_98_received = 0;
				call->senq_cnt = 0;
				q931_call_start_timer(call, T322);
			}
		}
	break;

	default:
		if (!q931_call_timer_running(call, T322)) {
			q931_call_send_status_enquiry(call, user_ies);
			call->senq_status_97_98_received = 0;
			call->senq_cnt = 0;
			q931_call_start_timer(call, T322);
		}
	break;
	}
}

void q931_suspend_reject_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SUSPEND-REJECT-REQ\n");

	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_call_send_suspend_reject(call, user_ies);
		q931_call_set_state(call, N10_ACTIVE);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_suspend_response(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SUSPEND-RESP\n");

	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_call_send_suspend_acknowledge(call, user_ies);
		q931_call_release_reference(call);
		q931_call_set_state(call, N0_NULL_STATE);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_suspend_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "SUSPEND-REQ\n");

	switch (call->state) {
	case U10_ACTIVE:
		q931_call_send_suspend(call, user_ies);
		q931_call_start_timer(call, T319);
		q931_call_set_state(call, U15_SUSPEND_REQUEST);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_restart_request(
	struct q931_call *call,
	const struct q931_ies *user_ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "RESTART-REQ\n");

	switch (call->state) {
	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING: {
		struct q931_channel *callchannel = call->channel;

		q931_call_stop_any_timer(call);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
			user_ies);
		q931_global_restart_confirm(&call->intf->global_call,
			call->dlc,
			callchannel);
	}
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING: {
		struct q931_channel *callchannel = call->channel;

		q931_call_stop_any_timer(call);
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, user_ies);
		q931_global_restart_confirm(&call->intf->global_call,
			call->dlc,
			callchannel);
	}
	break;
	}
}

void q931_int_alerting_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "INT-ALERTING-INDICATION\n");

	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);
		q931_call_start_timer(call, T301);
		q931_call_set_state(call, N7_CALL_RECEIVED);
		q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION, ies);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		q931_call_start_timer(call, T301);
		q931_call_set_state(call, N7_CALL_RECEIVED);
		q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_connect_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "INT-CONNECT-INDICATION\n");

	switch (call->state) {
	case N7_CALL_RECEIVED: {
		Q931_DECLARE_IES(causes);

		if (q931_channel_select_response(call, ies, &causes)) {
			call->preselected_ces = ces;
			q931_call_stop_timer(call, T301);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION, ies);
		} else {
			q931_ces_release_request(ces, &causes);
		}

		Q931_UNDECLARE_IES(causes);
	}
	break;

	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);
		call->preselected_ces = ces;
		q931_call_set_state(call, N8_CONNECT_REQUEST);
		q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION, ies);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		call->preselected_ces = ces;
		q931_call_set_state(call, N8_CONNECT_REQUEST);
		q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}

}

void q931_int_call_proceeding_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);

	report_call(call, LOG_DEBUG, "INT-CALL-PROCEEDING-IND\n");

	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		q931_call_start_timer(call, T310);
		q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
		q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_release_complete_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);
	assert(ces);

	report_call(call, LOG_DEBUG, "INT-RELEASE-COMPLETE-IND\n");

	switch (call->state) {
	case N0_NULL_STATE:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N19_RELEASE_REQUEST:
	case N25_OVERLAP_RECEIVING:
		q931_ces_free(ces);
	break;

	case N22_CALL_ABORT:
		q931_ces_free(ces);

		if (list_empty(&call->ces)) {
			if (call->disconnect_indication_sent) {
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					ies);
			}

			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
		}
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_info_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);
	assert(ces);

	report_call(call, LOG_DEBUG, "INT-INFO-IND\n");

	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N19_RELEASE_REQUEST:
	case N25_OVERLAP_RECEIVING:
		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_progress_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);
	assert(ces);

	report_call(call, LOG_DEBUG, "INT-PROGRESS-IND\n");

	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION, ies);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);
		q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION, ies);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION, ies);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_release_indication(
	struct q931_call *call,
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(call);
	assert(ces);

	report_call(call, LOG_DEBUG, "INT-RELEASE-IND\n");

	switch (call->state) {
	case N7_CALL_RECEIVED: {
		int able_to_proceed = 0;

		// Are other CES able to proceed?
		// FIXME Is this correct? Specs are unclear! FIXME

		struct q931_ces *tces = NULL;
		list_for_each_entry(tces, &call->ces, node) {
			if (tces != ces &&
			    tces->state != N0_NULL_STATE &&
			    tces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (able_to_proceed) {
			q931_ies_merge(&call->saved_cause, ies);
		} else {
			if (q931_call_timer_running(call, T312)) {
				q931_ies_merge(&call->saved_cause,
					ies);
			} else {
				q931_call_stop_timer(call, T301);
				q931_channel_release(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					ies);
			}
		}
	}
	break;

	case N8_CONNECT_REQUEST:
		if (call->preselected_ces) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, &ies);
			}

			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
			}

			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
					&ies);

			Q931_UNDECLARE_IES(ies);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING: {
		int able_to_proceed = 0;

		// Are other CES able to proceed?
		// FIXME Is this correct? Specs are unclear! FIXME

		struct q931_ces *tces = NULL;
		list_for_each_entry(tces, &call->ces, node) {
			if (tces != ces &&
			    tces->state != N0_NULL_STATE &&
			    tces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (able_to_proceed) {
			q931_ies_merge(&call->saved_cause, ies);
		} else {
			if (q931_call_timer_running(call, T312)) {
				q931_ies_merge(&call->saved_cause, ies);
			} else {
				q931_call_stop_timer(call, T310);
				q931_channel_release(call->channel);
				q931_call_release_reference(call);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					ies);
			}
		}
	}
	break;

	case N25_OVERLAP_RECEIVING: {
		int able_to_proceed = 0;

		// Are other CES able to proceed?
		// FIXME Is this correct? Specs are unclear! FIXME

		struct q931_ces *tces = NULL;
		list_for_each_entry(tces, &call->ces, node) {
			if (tces != ces &&
			    tces->state != N0_NULL_STATE &&
			    tces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (able_to_proceed) {
			q931_ies_merge(&call->saved_cause, ies);
		} else {
			if (q931_call_timer_running(call, T312)) {
				q931_ies_merge(&call->saved_cause, ies);
			} else {
				q931_channel_release(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					ies);
			}
		}
	}
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

static void q931_timer_T301(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T301 fired\n");

//	q931_call_primitive(call, Q931_CCB_TIMEOUT_INDICATION);

	switch (call->state) {
	case N7_CALL_RECEIVED: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
		memcpy(cause->diagnostics, "301", 3);
		cause->diagnostics_len = 3;
		q931_ies_add_put(&ies, &cause->ie);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, &ies);
			}

			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
			}
		} else {
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}

		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T302(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T302 fired\n");

	switch (call->state) {
	case N2_OVERLAP_SENDING: {
		/* Simulates a sending complete to the CCB */

		Q931_DECLARE_IES(ies);
		struct q931_ie_sending_complete *sc =
			q931_ie_sending_complete_alloc();
		q931_ies_add_put(&ies, &sc->ie);

		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &ies);
		Q931_UNDECLARE_IES(ies);
	}

#if 0
		if (q931_is_number_complete(call)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
			q931_ies_add_put(&ies, &cause->ie);

			if (call->tones_option) {
				struct q931_ie_progress_indicator *pi =
					q931_ie_progress_indicator_alloc();
				pi->coding_standard = Q931_IE_PI_CS_CCITT;
				pi->location =
					q931_ie_progress_indicator_location(call);
				pi->progress_description =
					Q931_IE_PI_PD_IN_BAND_INFORMATION;
				q931_ies_add_put(&ies, &pi->ie);

				q931_call_send_disconnect(call, &ies);
				q931_channel_start_tone(call->channel,
					Q931_TONE_HANGUP);
				q931_call_start_timer(call, T306);
			} else {
				q931_channel_disconnect(call->channel);
				q931_call_send_disconnect(call, &ies);
				q931_call_start_timer(call, T305);
			}

			q931_call_set_state(call, N12_DISCONNECT_INDICATION);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_send_call_proceeding(call, NULL);
			q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);
			q931_call_primitive(call, Q931_CCB_TIMEOUT_INDICATION, NULL);
		}
#endif
	break;

	case U25_OVERLAP_RECEIVING: {
		/* Simulates a sending complete to the CCB */

		Q931_DECLARE_IES(ies);
		struct q931_ie_sending_complete *sc =
			q931_ie_sending_complete_alloc();
		q931_ies_add_put(&ies, &sc->ie);

		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &ies);
		Q931_UNDECLARE_IES(ies);
	}
#if 0
		if (q931_is_number_complete(call)) {
			q931_channel_disconnect(call->channel);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, U11_DISCONNECT_REQUEST);

			Q931_UNDECLARE_IES(ies);
		} else {
			// FIXME TODO Which message???
			// Connect => q931_call_start_timer(call, T313);
			//		q931_call_set_state(call, U8_CONNECT_REQUEST);
			// Alerting => q931_call_set_state(call, U7_CALL_RECEIVED);
			// Callproc => q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);
		}
#endif
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T303(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T303 fired\n");

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (!call->T303_fired) {
			if (call->broadcast_setup) {
				if (call->release_complete_received) {
					q931_channel_release(
						call->channel);
					q931_call_set_state(call,
						N22_CALL_ABORT);
					q931_call_primitive(call,
						Q931_CCB_RELEASE_INDICATION,
						NULL);
				} else {
					q931_call_send_setup_bc(call,
						&call->setup_ies);
					q931_call_start_timer(call, T303);
					q931_call_restart_timer(call, T312);
				}
			} else {
				q931_call_send_setup(call,
					&call->setup_ies);
				q931_call_start_timer(call, T303);
			}
		} else {
			if (call->broadcast_setup) {
				q931_channel_release(call->channel);
				q931_call_set_state(call, N22_CALL_ABORT);


				Q931_DECLARE_IES(ies);
				struct q931_ie_cause *cause =
						q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location =
					q931_ie_cause_location_call(call);
				cause->value = Q931_IE_C_CV_NO_USER_RESPONDING;
				q931_ies_add_put(&ies, &cause->ie);

				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					NULL);
			} else {
				q931_call_start_timer(call, T305);

				Q931_DECLARE_IES(ies);

				struct q931_ie_cause *cause =
					q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location =
					q931_ie_cause_location_call(call);
				cause->value =
					Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
				memcpy(cause->diagnostics, "303", 3);
				cause->diagnostics_len = 3;
				q931_ies_add_put(&ies, &cause->ie);

				q931_call_send_disconnect(call, &ies);
				q931_call_set_state(call,
					N12_DISCONNECT_INDICATION);
				q931_call_primitive(call,
					Q931_CCB_DISCONNECT_INDICATION,
					NULL);

				Q931_UNDECLARE_IES(ies);
			}
		}
	break;

	case U1_CALL_INITIATED:
		if (!call->T303_fired) {
			q931_call_send_setup(call, &call->setup_ies);
			q931_call_start_timer(call, T303);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "303", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			/* This is not specified by the standard, however it
			 * seems to be reasonable.
			 */
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION,
				NULL);

			q931_call_send_release_complete(call, &ies);
			q931_call_set_state(call, U0_NULL_STATE);
			q931_call_release_reference(call);

			Q931_UNDECLARE_IES(ies);
		}
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	call->T303_fired = TRUE;

	q931_call_put(call);
}

static void q931_timer_T304(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T304 fired\n");

	switch (call->state) {
	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
			}

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "304", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, &ies);
			}

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "304", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_disconnect(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);

			Q931_UNDECLARE_IES(ies);
		}

		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);
	break;

	case U2_OVERLAP_SENDING:
		q931_channel_disconnect(call->channel);

		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
		memcpy(cause->diagnostics, "304", 3);
		cause->diagnostics_len = 3;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive1(call, Q931_CCB_SETUP_CONFIRM,
			NULL, Q931_SETUP_CONFIRM_ERROR);

		Q931_UNDECLARE_IES(ies);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}


static void q931_timer_T305(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T305 fired\n");

	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T306(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T306 fired\n");

	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		q931_channel_stop_tone(call->channel);
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T308(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T308 fired\n");

	switch (call->state) {
	case N19_RELEASE_REQUEST: {
		if (call->T308_fired) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "308", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ies_merge(&ies, &call->release_cause);

			q931_call_send_release(call, &ies);
			q931_call_start_timer(call, T308);

			Q931_UNDECLARE_IES(ies);
		} else {
			if (call->channel) {
				if (call->intf->mode ==
						LAPD_INTF_MODE_MULTIPOINT) {
					q931_channel_release(call->channel);
				} else {
					q931_channel_set_state(call->channel,
						Q931_CHANSTATE_MAINTAINANCE);

#if 0
					struct q931_chanset cs;
					q931_chanset_init(&cs);
					q931_chanset_add(&cs, call->channel);

					/* Italian network doesnt require this */
					q931_management_restart_request(
						&call->intf->global_call,
						call->dlc,
						&cs, NULL);
#endif
				}
			}

			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call, Q931_CCB_RELEASE_CONFIRM,
				NULL, Q931_RELEASE_CONFIRM_OK);
		}
	}
	break;

	case U19_RELEASE_REQUEST:
		if (!call->T308_fired) {
			q931_call_send_release(call, NULL);
			q931_call_start_timer(call, T308);
		} else {
			if (call->intf->mode != LAPD_INTF_MODE_MULTIPOINT &&
			    call->channel) {
				q931_channel_set_state(call->channel,
					Q931_CHANSTATE_MAINTAINANCE);

#if 0
				struct q931_chanset cs;
				q931_chanset_init(&cs);
				q931_chanset_add(&cs, call->channel);

				/* Italian network doesnt require this */
				q931_management_restart_request(
					&call->intf->global_call,
					call->dlc,
					&cs, NULL);
#endif
			}

			q931_channel_release(call->channel);
			q931_call_set_state(call, U0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call, Q931_CCB_RELEASE_CONFIRM,
				NULL, Q931_RELEASE_CONFIRM_ERROR);
		}
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	call->T308_fired = TRUE;

	q931_call_put(call);
}

static void q931_timer_T309(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T309 fired\n");

	switch (call->state) {
	case N10_ACTIVE:
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);
	break;

	case U10_ACTIVE:
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T310(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T310 fired\n");

	switch (call->state) {
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			q931_channel_release(call->channel);

			if (q931_call_timer_running(call, T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_release_reference(call);
			}

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "310", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ies_merge(&ies, &call->release_cause);

			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, &ies);
			}

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "310", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_release(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);

			Q931_UNDECLARE_IES(ies);
		}

		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T312(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T312 fired\n");

	switch (call->state) {
	case N7_CALL_RECEIVED: {
		int able_to_proceed = 0;
		struct q931_ces *ces;
		list_for_each_entry(ces, &call->ces, node) {
			if (ces->state != N0_NULL_STATE &&
			    ces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (!able_to_proceed) {
			q931_call_stop_timer(call, T301);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
				NULL);
		}
	}
	break;

	case N9_INCOMING_CALL_PROCEEDING: {
		int able_to_proceed = 0;
		struct q931_ces *ces;
		list_for_each_entry(ces, &call->ces, node) {
			if (ces->state != N0_NULL_STATE &&
			    ces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (!able_to_proceed) {
			q931_call_stop_timer(call, T310);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION,
				NULL);
		}
	}
	break;

	case N22_CALL_ABORT:
		if (list_empty(&call->ces)) {

			/* This condition is probably wrong, FIXME */
			if (call->disconnect_indication_sent)
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					NULL);

			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
		}
	break;

	case N25_OVERLAP_RECEIVING: {
		int able_to_proceed = 0;

		// Are other CES able to proceed?
		// FIXME Is this correct? Specs are unclear! FIXME

		struct q931_ces *ces = NULL;
		list_for_each_entry(ces, &call->ces, node) {
			if (ces->state != N0_NULL_STATE &&
			    ces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (!able_to_proceed) {
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
				NULL);
		}
	}
	break;

	default:
		// Do nothing
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T313(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T313 fired\n");

	switch (call->state) {
	case U8_CONNECT_REQUEST:
		q931_channel_disconnect(call->channel);

		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
		memcpy(cause->diagnostics, "313", 3);
		cause->diagnostics_len = 3;
		q931_ies_add_put(&ies, &cause->ie);


		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive1(call, Q931_CCB_SETUP_COMPLETE_INDICATION,
			NULL, Q931_SETUP_COMPLETE_INDICATION_ERROR);

		Q931_UNDECLARE_IES(ies);
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T314(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T314 fired\n");

	report_call(call, LOG_WARNING,
		"Unexpected timer T314\n");

	q931_call_put(call);
}

static void q931_timer_T316(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T316 fired\n");

	report_call(call, LOG_WARNING,
		"Unexpected timer T316\n");

	q931_call_put(call);
}

static void q931_timer_T318(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T318 fired\n");

	switch (call->state) {
	case U17_RESUME_REQUEST: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
		memcpy(cause->diagnostics, "318", 3);
		cause->diagnostics_len = 3;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_release(call, &ies);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
		q931_call_primitive1(call, Q931_CCB_RESUME_CONFIRM,
			NULL, Q931_RESUME_CONFIRM_TIMEOUT);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	default:
		q931_call_unexpected_timer(call);
	break;
	}

	q931_call_put(call);
}

static void q931_timer_T319(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T319 fired\n");

	if (call->state == N15_SUSPEND_REQUEST) {
		q931_call_primitive1(call, Q931_CCB_SUSPEND_CONFIRM,
			NULL, Q931_SUSPEND_CONFIRM_TIMEOUT);
		q931_call_set_state(call, U10_ACTIVE);
	} else {
		q931_call_unexpected_timer(call);
	}

	q931_call_put(call);
}

static void q931_timer_T320(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T320 fired\n");

	report_call(call, LOG_WARNING,
		"Unexpected timer T320\n");

	q931_call_put(call);
}

static void q931_timer_T321(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T321 fired\n");

	report_call(call, LOG_WARNING,
		"Unexpected timer T321\n");

	q931_call_put(call);
}

static void q931_timer_T322(void *data)
{
	assert(data);

	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T322 fired\n");

	if (call->state == N0_NULL_STATE ||
	    call->state == U0_NULL_STATE) {
		q931_call_unexpected_timer(call);

		q931_call_put(call);
		return;
	}

	// Status received
	if (call->senq_status_97_98_received) {
		// Implementation dependent
	} else {
		if (call->senq_cnt > 3) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_TEMPORARY_FAILURE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
				&ies); // With error?

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_send_status_enquiry(call, NULL);
			q931_call_start_timer(call, T322);
			call->senq_cnt++;
		}
	}

	q931_call_put(call);
}

static inline void q931_handle_alerting(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T303);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
					&msg->ies);
				q931_ces_alerting_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T303);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
					&msg->ies);
			} else {
				q931_call_send_release(call, &causes);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
					&causes);
			}

			Q931_UNDECLARE_IES(causes);
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call,
							&msg->ies, &causes)) {
				q931_ces_alerting_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T310);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
					&msg->ies);
				q931_ces_alerting_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_start_timer(call, T301);
			q931_call_set_state(call, N7_CALL_RECEIVED);
			q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
				&msg->ies);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, msg->dlc);

		// FIXME (see 5.2.5.4 5.3.2(e))
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//	 IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
					&msg->ies);
				q931_ces_alerting_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_start_timer(call, T301);
			q931_call_set_state(call, N7_CALL_RECEIVED);
			q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
				&msg->ies);
		}
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		q931_channel_control(call->channel);
		q931_call_set_state(call, U4_CALL_DELIVERED);
		q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
			&msg->ies);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_control(call->channel);
		q931_call_set_state(call, U4_CALL_DELIVERED);
		q931_call_primitive(call, Q931_CCB_ALERTING_INDICATION,
			&msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_call_proceeding(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T303);
				q931_call_start_timer(call, T310);
				q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION,
					&msg->ies);
				q931_ces_call_proceeding_request(ces, &msg->ies);
				q931_call_set_state(call,
					 N9_INCOMING_CALL_PROCEEDING);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T303);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies,
			    &causes)) {
				q931_call_start_timer(call, T310);
				q931_call_set_state(call,
					N9_INCOMING_CALL_PROCEEDING);
				q931_call_primitive(call,
					Q931_CCB_PROCEEDING_INDICATION,
					&msg->ies);
			} else {
				q931_call_send_release(call, &causes);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
					&causes);
			}

			Q931_UNDECLARE_IES(causes);
		}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_ces_call_proceeding_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, msg->dlc);

		// FIXME (see 5.2.5.4 5.3.2(e))
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//	 IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_start_timer(call, T310);
				q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
				q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION,
					&msg->ies);
				q931_ces_call_proceeding_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_start_timer(call, T310);
			q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
			q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION,
				&msg->ies);
		}
	break;

	case U1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T303);

		Q931_DECLARE_IES(causes);

		if (q931_channel_select_response(call, &msg->ies, &causes)) {
			q931_call_set_state(call, U3_OUTGOING_CALL_PROCEEDING);
			q931_channel_control(call->channel);
			q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION,
				&msg->ies);
		} else {
			q931_call_send_release(call, &causes);
			q931_call_start_timer(call, T308);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
				&causes);
		}

		Q931_UNDECLARE_IES(causes);
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		q931_channel_control(call->channel);
		q931_call_set_state(call, U3_OUTGOING_CALL_PROCEEDING);
		q931_call_primitive(call, Q931_CCB_PROCEEDING_INDICATION,
			&msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_connect(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies,
								&causes)) {
				q931_call_stop_timer(call, T303);
				call->preselected_ces = ces;
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive1(call,
					Q931_CCB_SETUP_CONFIRM,
					&msg->ies, Q931_SETUP_CONFIRM_OK);
				q931_ces_connect_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T303);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies,
								&causes)) {
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive1(call,
					Q931_CCB_SETUP_CONFIRM,
					&msg->ies, Q931_SETUP_CONFIRM_OK);
			} else {
				q931_call_send_release(call, &causes);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call,
					Q931_CCB_RELEASE_INDICATION,
					&causes);
			}

			Q931_UNDECLARE_IES(causes);
		}
	break;

	case N7_CALL_RECEIVED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T301);
				call->preselected_ces = ces;
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
					&msg->ies);
				q931_ces_connect_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T301);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
				&msg->ies);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T310);
				call->preselected_ces = ces;
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
					&msg->ies);
				q931_ces_connect_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
				&msg->ies);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, msg->dlc);

		// FIXME (see 5.2.5.4 5.3.2(e))
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//	 IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		}

	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				call->preselected_ces = ces;
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
					&msg->ies);
				q931_ces_connect_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, Q931_CCB_CONNECT_INDICATION,
				&msg->ies);
		}
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_send_connect_acknowledge(call, NULL);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_SETUP_CONFIRM, &msg->ies,
			Q931_SETUP_CONFIRM_OK);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_send_connect_acknowledge(call, NULL);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_SETUP_CONFIRM, &msg->ies,
			Q931_SETUP_CONFIRM_OK);
	break;

	case U4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_send_connect_acknowledge(call, NULL);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_SETUP_CONFIRM, &msg->ies,
			Q931_SETUP_CONFIRM_OK);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_connect_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N10_ACTIVE:
		// Do nothing
	break;

	case U8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T313);
		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_SETUP_COMPLETE_INDICATION,
			&msg->ies, Q931_SETUP_COMPLETE_INDICATION_OK);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_progress(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state(call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION,
				&msg->ies);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state(call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_stop_timer(call, T310);
			q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION,
				&msg->ies);
		}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state(call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_primitive(call, Q931_CCB_PROGRESS_INDICATION,
				&msg->ies);
		}
	break;


	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_control(call->channel);

		int i;
		struct q931_ie_progress_indicator *pi = NULL;

		for(i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_PROGRESS_INDICATOR) {
				pi = container_of(msg->ies.ies[i],
					struct q931_ie_progress_indicator, ie);
				break;
			}
		}

		if (pi->progress_description ==
				Q931_IE_PI_PD_CALL_NOT_END_TO_END ||
		    pi->progress_description ==
				Q931_IE_PI_PD_DESTINATION_ADDRESS_IS_NON_ISDN)
			q931_call_stop_timer(call, T304);

		q931_call_primitive(call,
			Q931_CCB_PROGRESS_INDICATION, &msg->ies);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_control(call->channel);
		q931_call_primitive(call,
			Q931_CCB_PROGRESS_INDICATION, &msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}


static void q931_handle_setup(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N0_NULL_STATE: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id ==
						Q931_IE_CALLED_PARTY_NUMBER) {

				struct q931_ie_called_party_number *cdpn;
				cdpn = container_of(msg->ies.ies[i],
					struct q931_ie_called_party_number, ie);

				if (strlen(cdpn->number))
					call->digits_dialled = TRUE;
			}
		}

		Q931_DECLARE_IES(ies);

		int bumping_needed;
		struct q931_channel *chan =
			 q931_channel_select_setup(call, &msg->ies, &ies,
				&bumping_needed);

		if (chan) {
			call->channel = chan;
			chan->call = call;
			q931_channel_set_state(chan,
					Q931_CHANSTATE_SELECTED);

			q931_ies_copy(&call->setup_ies, &msg->ies);

			q931_call_set_state(call, N1_CALL_INITIATED);
			q931_call_primitive(call,
				Q931_CCB_SETUP_INDICATION, &msg->ies);
		} else {
			q931_call_send_release_complete(call, &ies);
			q931_call_release_reference(call);
		}

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case U0_NULL_STATE: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		Q931_DECLARE_IES(ies);

		int bumping_needed;
		call->channel =
			q931_channel_select_setup(call, &msg->ies, &ies,
						&bumping_needed);

		if (!call->channel) {
			q931_call_send_release_complete(call, &ies);
			q931_call_set_state(call, U0_NULL_STATE);
			q931_call_release_reference(call);
		} else {
			call->channel->call = call;

			q931_ies_copy(&call->setup_ies, &msg->ies);

			q931_call_set_state(call, U6_CALL_PRESENT);
			q931_call_primitive(call,
				Q931_CCB_SETUP_INDICATION, &msg->ies);
		}

		Q931_UNDECLARE_IES(ies);
	}
	break;

	default:
		// Do nothing
	break;
	}
}

static void q931_handle_setup_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N6_CALL_PRESENT: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_stop_timer(call, T303);

				struct q931_ces *ces;
				ces = q931_ces_alloc(call, msg->dlc);

				q931_call_start_timer(call, T304);
				q931_call_set_state(call, N25_OVERLAP_RECEIVING);
				q931_call_primitive(call, Q931_CCB_MORE_INFO_INDICATION,
					&msg->ies);
				q931_ces_setup_ack_request(ces, &msg->ies);
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call, msg->dlc);

				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_stop_timer(call, T303);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_call_start_timer(call, T304);
				q931_call_set_state(call, N25_OVERLAP_RECEIVING);
				q931_call_primitive(call, Q931_CCB_MORE_INFO_INDICATION,
					&msg->ies);
			} else {
				q931_call_send_release(call, &causes);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
					&causes);
			}

			Q931_UNDECLARE_IES(causes);
		}
	}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);

			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_ces_setup_ack_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NON_SELECTED_USER_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, msg->dlc);

		// FIXME (see 5.2.5.4 5.3.2(e))
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//	 IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			memcpy(cause->diagnostics, "312", 3);
			cause->diagnostics_len = 3;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_release_request(ces, &ies);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			struct q931_ces *ces;
			ces = q931_ces_alloc(call, msg->dlc);

			Q931_DECLARE_IES(causes);
			if (q931_channel_select_response(call, &msg->ies, &causes)) {
				q931_ces_setup_ack_request(ces, &msg->ies);
			} else {
				q931_ces_release_request(ces, &causes);
			}

			Q931_UNDECLARE_IES(causes);
		} else {
			q931_call_message_not_compatible_with_state(call, msg);
		}
	break;

	case U1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T303);

		Q931_DECLARE_IES(causes);
		if (q931_channel_select_response(call, &msg->ies, &causes)) {
			q931_call_start_timer(call, T304);
			q931_call_set_state(call, U2_OVERLAP_SENDING);
			q931_channel_control(call->channel);
			q931_call_primitive(call, Q931_CCB_MORE_INFO_INDICATION,
				&msg->ies);
		} else {
			q931_call_send_release(call, &causes);
			q931_call_start_timer(call, T308);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION,
				&causes);
		}

		Q931_UNDECLARE_IES(causes);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_save_cause(struct q931_ies *ies, struct q931_ies *src_ies)
{
	int i;
	for (i=0; i<src_ies->count; i++) {
		if (src_ies->ies[i]->cls->id == Q931_IE_CAUSE)
			q931_ies_add(ies, src_ies->ies[i]);
	}
}

static void q931_handle_disconnect(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state_n6(
								call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_stop_timer(call, T301);
			q931_channel_disconnect(call->channel);
			q931_save_cause(&call->disconnect_cause, &msg->ies);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call,
				Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state_n6(
								call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_channel_release(call->channel);
			q931_save_cause(&call->disconnect_cause, &msg->ies);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call,
				Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state_n6(
								call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_stop_timer(call, T310);
			q931_channel_disconnect(call->channel);
			q931_save_cause(&call->disconnect_cause, &msg->ies);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call,
				Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
		}
	break;

	case N10_ACTIVE:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N12_DISCONNECT_INDICATION:
		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_channel_stop_tone(call->channel);
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N17_RESUME_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_disconnect(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case N19_RELEASE_REQUEST:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state_n6(
								call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_stop_timer(call, T304);
			q931_channel_disconnect(call->channel);
			q931_save_cause(&call->disconnect_cause, &msg->ies);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call,
				Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
		}
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		q931_channel_control(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_control(call->channel);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U10_ACTIVE:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	case U8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T313);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_call_stop_timer(call, T305);
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T319);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		q931_call_send_release(call, &call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

	case U19_RELEASE_REQUEST:
		// Do nothing
	break;

	case U25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_save_cause(&call->disconnect_cause, &msg->ies);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call,
			Q931_CCB_DISCONNECT_INDICATION, &msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_release(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N0_NULL_STATE:
	case U0_NULL_STATE: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_release_complete(call, &ies);
		q931_call_release_reference(call);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case N1_CALL_INITIATED:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_send_release_complete(call, NULL);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T303);
			q931_channel_release(call->channel);
			q931_call_release_reference(call);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N7_CALL_RECEIVED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_send_release_complete(call, NULL);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T302);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_send_release_complete(call, NULL);

		if (!call->broadcast_setup) {
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_send_release_complete(call, NULL);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T310);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N12_DISCONNECT_INDICATION:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_channel_stop_tone(call->channel);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T308);
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call,
			Q931_CCB_RELEASE_CONFIRM, &msg->ies,
			Q931_RELEASE_CONFIRM_OK);
	break;

	case N22_CALL_ABORT:
		q931_call_send_release_complete(call, NULL);
	break;

	case N25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			q931_call_send_release_complete(call, NULL);
		} else {
			q931_call_stop_timer(call, T304);
			q931_channel_release(call->channel);
			q931_call_send_release_complete(call, NULL);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case U1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T303);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U12_DISCONNECT_INDICATION:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T313);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U11_DISCONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T305);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T319);
		q931_channel_release(call->channel);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U17_RESUME_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T318);
		q931_call_send_release_complete(call, NULL);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T308);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call, Q931_CCB_RELEASE_CONFIRM, &msg->ies,
			Q931_RELEASE_CONFIRM_OK);
	break;

	case U25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_call_send_release_complete(call, NULL);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}

}

static void q931_handle_release_complete(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	call->release_complete_received = TRUE;

	switch (call->state) {
	case N0_NULL_STATE:
		// Do nothing
	break;

	case N1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_release_reference(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_channel_release(call->channel);
		q931_call_release_reference(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_release_reference(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N6_CALL_PRESENT:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (call->broadcast_setup) {
			q931_ies_copy(&call->saved_cause, &msg->ies);
		} else {
			q931_call_stop_timer(call, T303);
			q931_channel_release(call->channel);
			q931_call_release_reference(call);
			q931_call_set_state(call, N0_NULL_STATE);

			/* EN 300 403-2 says that the primitive should be
			 * RELEASE-INDICATION, instead, REJECT-INDICATION seems
			 * more sensible:
			 *
			 * q931_call_primitive(call,
			 *	Q931_CCB_RELEASE_INDICATION, &msg->ies);
			 *
			 */

			q931_call_primitive(call,
				Q931_CCB_REJECT_INDICATION, &msg->ies);
		}
	break;

	case N7_CALL_RECEIVED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T301);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (!call->broadcast_setup) {
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T310);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N12_DISCONNECT_INDICATION:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_channel_stop_tone(call->channel);
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case N19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T308);
		q931_channel_release(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call,
			Q931_CCB_RELEASE_CONFIRM, &msg->ies,
			Q931_RELEASE_CONFIRM_OK);
	break;

	case N22_CALL_ABORT:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T304);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		}
	break;

	case U0_NULL_STATE:
		// Do nothing
	break;

	case U1_CALL_INITIATED:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		/* Not in EN 300 403-2, is this acceptable too? */
		q931_channel_release(call->channel);

		q931_call_stop_timer(call, T303);
		q931_call_release_reference(call);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_primitive(call,
			Q931_CCB_REJECT_INDICATION, &msg->ies);
	break;

	case U2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T304);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U12_DISCONNECT_INDICATION:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U8_CONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T313);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U11_DISCONNECT_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T305);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T319);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U17_RESUME_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T318);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive(call,
			Q931_CCB_RELEASE_INDICATION, &msg->ies);
	break;

	case U19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T308);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call,
			Q931_CCB_RELEASE_CONFIRM, &msg->ies,
			Q931_RELEASE_CONFIRM_OK);
	break;

	case U25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call,
			Q931_CCB_RELEASE_CONFIRM, &msg->ies,
			Q931_RELEASE_CONFIRM_OK);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_status(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N0_NULL_STATE: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			}
		}

		if (cs->value != q931_call_state_to_ie_state(N0_NULL_STATE)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
			memcpy(cause->diagnostics, &msg->raw_message_type,
				sizeof(msg->raw_message_type));
			cause->diagnostics_len = sizeof(msg->raw_message_type);

			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
			q931_call_start_timer(call, T308);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case N19_RELEASE_REQUEST: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			}
		}

		if (cs->value == q931_call_state_to_ie_state(N0_NULL_STATE)) {
			q931_call_stop_timer(call, T308);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call,
				Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
		}
	}
	break;

	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		Q931_DECLARE_IES(ies);
		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			} else if (msg->ies.ies[i]->cls->id == Q931_IE_CAUSE) {
				q931_ies_add_put(&ies, msg->ies.ies[i]);
			}
		}

		if (q931_call_timer_running(call, T322)) {
			if (q931_ies_contain_cause(&ies,
				Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED) ||
			    q931_ies_contain_cause(&ies,
				Q931_IE_C_CV_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_STATE)) {
				// Save cause and call state (!?!?!?)
				goto skip;
			} else if(q931_ies_contain_cause(&ies,
					Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY)) {
				q931_call_stop_timer(call, T322);
			}
		}

skip:
		Q931_UNDECLARE_IES(ies);

		if (cs->value == q931_call_state_to_ie_state(N0_NULL_STATE)) {
			q931_call_stop_any_timer(call);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call,
				Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		} else if (cs->value ==
				q931_call_state_to_ie_state(call->state)) {
				// NOTE 1. FURTHER ACTIONS ARE AN
				// IMPLEMENTATION OPTION.
		} else {
			q931_call_stop_any_timer(call);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
			memcpy(cause->diagnostics, &msg->raw_message_type,
				sizeof(msg->raw_message_type));
			cause->diagnostics_len = sizeof(msg->raw_message_type);

			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);

			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
			q931_call_primitive1(call,
				Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case U0_NULL_STATE: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			}
		}

		if (cs->value != q931_call_state_to_ie_state(U0_NULL_STATE)) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
			memcpy(cause->diagnostics, &msg->raw_message_type,
				sizeof(msg->raw_message_type));
			cause->diagnostics_len = sizeof(msg->raw_message_type);

			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);

			q931_call_set_state(call, U19_RELEASE_REQUEST);
			q931_call_start_timer(call, T308);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case U19_RELEASE_REQUEST: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			}
		}

		if (cs->value == q931_call_state_to_ie_state(U0_NULL_STATE)) {
			q931_call_stop_timer(call, T308);
			q931_channel_release(call->channel);
			q931_call_set_state(call, U0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call, Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
		}
	}
	break;

	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U25_OVERLAP_RECEIVING: {
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		Q931_DECLARE_IES(ies);
		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			} else if (msg->ies.ies[i]->cls->id == Q931_IE_CAUSE) {
				q931_ies_add_put(&ies, msg->ies.ies[i]);
			}
		}

		if (q931_call_timer_running(call, T322)) {
			if (q931_ies_contain_cause(&ies,
				Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED) ||
			    q931_ies_contain_cause(&ies,
				Q931_IE_C_CV_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_STATE)) {
				// Save cause and call state (!?!?!?)
				goto skip2;
			} else if(q931_ies_contain_cause(&ies,
					Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY)) {
				q931_call_stop_timer(call, T322);
			}
		}

skip2:
		Q931_UNDECLARE_IES(ies);

		if (cs->value == q931_call_state_to_ie_state(U0_NULL_STATE)) {
			q931_call_stop_any_timer(call);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);
			q931_call_primitive1(call,
				Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);
		} else if (cs->value == q931_call_state_to_ie_state(call->state)) {
				// NOTE 1. FURTHER ACTIONS ARE AN IMPLEMENTATION OPTION.
		} else {
			q931_call_stop_any_timer(call);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
			memcpy(cause->diagnostics, &msg->raw_message_type,
				sizeof(msg->raw_message_type));
			cause->diagnostics_len = sizeof(msg->raw_message_type);

			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);

			q931_call_start_timer(call, T308);
			q931_call_set_state(call, U19_RELEASE_REQUEST);
			q931_call_primitive1(call,
				Q931_CCB_STATUS_INDICATION, &msg->ies,
				Q931_STATUS_INDICATION_ERROR);
			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &msg->ies);

			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_status_enquiry(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	Q931_DECLARE_IES(ies);

	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(call);
	cause->value = Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY;
	q931_ies_add_put(&ies, &cause->ie);

	struct q931_ie_call_state *cs = q931_ie_call_state_alloc();
	cs->coding_standard = Q931_IE_CS_CS_CCITT;
	cs->value = q931_call_state_to_ie_state(call->state);
	q931_ies_add_put(&ies, &cs->ie);

	q931_call_send_status(call, &ies);

	Q931_UNDECLARE_IES(ies);
}

#if 0
static void q931_handle_user_information(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);


}

static void q931_handle_segment(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_congestion_control(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}
#endif

static void q931_handle_info(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N2_OVERLAP_SENDING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T302);
		q931_channel_stop_tone(call->channel);
		q931_call_start_timer(call, T302);
		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &msg->ies);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &msg->ies);
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_call_message_not_compatible_with_state_n6(
								call, msg);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_primitive(call,
				Q931_CCB_INFO_INDICATION, &msg->ies);
		}
	break;

	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U19_RELEASE_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &msg->ies);
	break;

	case U25_OVERLAP_RECEIVING:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_restart_timer(call, T302);
		q931_call_primitive(call, Q931_CCB_INFO_INDICATION, &msg->ies);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

#if 0
static void q931_handle_facility(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_register(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}
#endif

static void q931_handle_notify(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N10_ACTIVE:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_primitive(call, Q931_CCB_NOTIFY_INDICATION, &msg->ies);
	break;

	default:
		// Page 154 subclause 5.9 in ETSI EN 300 403-1 V1.3.2 permits
		// the reception of NOTIFY in any state

		// So, do nothing
	break;
	}
}

#if 0
static void q931_handle_hold(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_hold_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_hold_reject(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_retrieve(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_retrieve_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}

static void q931_handle_retrieve_reject(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

}
#endif

static void q931_handle_resume(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N0_NULL_STATE:
		if (call->intf->type != LAPD_INTF_TYPE_BRA) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);

			Q931_UNDECLARE_IES(ies);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_set_state(call, N17_RESUME_REQUEST);
			q931_call_primitive(call, Q931_CCB_RESUME_INDICATION, &msg->ies);
		}
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_resume_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case U17_RESUME_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T318);
		// Handle failed connection TODO
		q931_channel_connect(call->channel);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_RESUME_CONFIRM, &msg->ies,
			Q931_RESUME_CONFIRM_OK);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_resume_reject(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case U17_RESUME_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T318);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call, Q931_CCB_RESUME_CONFIRM, &msg->ies,
			Q931_RESUME_CONFIRM_ERROR);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_suspend(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case N10_ACTIVE: {
		if (call->intf->type != LAPD_INTF_TYPE_BRA) {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_FACILITY_REJECTED;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_suspend_reject(call, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			if (q931_call_decode_ies(call, msg) < 0)
				break;

			q931_call_set_state(call, N15_SUSPEND_REQUEST);
			q931_call_primitive(call, Q931_CCB_SUSPEND_INDICATION, &msg->ies);
		}
	}
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_suspend_acknowledge(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T319);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);
		q931_call_primitive1(call, Q931_CCB_SUSPEND_CONFIRM, &msg->ies,
			Q931_SUSPEND_CONFIRM_OK);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

static void q931_handle_suspend_reject(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);

	switch (call->state) {
	case U15_SUSPEND_REQUEST:
		if (q931_call_decode_ies(call, msg) < 0)
			break;

		q931_call_stop_timer(call, T319);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive1(call, Q931_CCB_SUSPEND_CONFIRM, &msg->ies,
			Q931_SUSPEND_CONFIRM_ERROR);
	break;

	default:
		q931_call_message_not_compatible_with_state(call, msg);
	break;
	}
}

void q931_call_dl_establish_indication(struct q931_call *call)
{
	assert(call);

	Q931_DECLARE_IES(ies);

	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(call);
	cause->value = Q931_IE_C_CV_TEMPORARY_FAILURE;
	q931_ies_add_put(&ies, &cause->ie);

	switch (call->state) {
	case N2_OVERLAP_SENDING: {
		q931_call_stop_any_timer(call);

		if (call->tones_option) {
			struct q931_ie_progress_indicator *pi =
				q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location = q931_ie_progress_indicator_location(call);
			pi->progress_description = Q931_IE_PI_PD_IN_BAND_INFORMATION;
			q931_ies_add_put(&ies, &pi->ie);

			q931_call_send_disconnect(call, &ies);
			q931_channel_start_tone(call->channel, Q931_TONE_FAILURE);
			q931_call_start_timer(call, T306);
		} else {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		q931_call_primitive(call, Q931_CCB_DISCONNECT_INDICATION, NULL);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (!call->broadcast_setup) {
			q931_channel_disconnect(call->channel);
			q931_call_send_disconnect(call, &ies);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
			q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, NULL);
		}
	break;

	case U2_OVERLAP_SENDING: {
		q931_call_stop_timer(call, T304);
		q931_channel_release(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive(call, Q931_CCB_DISCONNECT_INDICATION, NULL);
	}
	break;

	case U25_OVERLAP_RECEIVING: {
		q931_call_stop_timer(call, T302);
		q931_channel_release(call->channel);
		q931_call_send_disconnect(call, &ies);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive(call, Q931_CCB_DISCONNECT_INDICATION, NULL);
	}
	break;

	default:
		// Do nothing
	break;
	}

	Q931_UNDECLARE_IES(ies);
}

void q931_call_dl_establish_confirm(struct q931_call *call)
{
	assert(call);

	Q931_DECLARE_IES(ies);

	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(call);
	cause->value = Q931_IE_C_CV_NORMAL_UNSPECIFIED;
	q931_ies_add_put(&ies, &cause->ie);

	if (call->state == N10_ACTIVE ||
	    call->state == U10_ACTIVE) {
		q931_call_stop_timer(call, T309);
		q931_call_send_call_status(call, &ies);
	}

	Q931_UNDECLARE_IES(ies);
}

void q931_call_dl_release_indication(struct q931_call *call)
{
	assert(call);

	switch (call->state) {
	case N0_NULL_STATE:
		// Do nothing
	break;

	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
		if (!call->broadcast_setup || call->state == N6_CALL_PRESENT) {
			q931_call_stop_any_timer(call);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);

			Q931_DECLARE_IES(ies);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &ies);

			Q931_UNDECLARE_IES(ies);
		}
	break;

	case N10_ACTIVE:
		if (q931_call_timer_running(call, T309)) {
			q931_call_stop_timer(call, T309);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);

			Q931_DECLARE_IES(ies);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_start_timer(call, T309);

			if (connect(call->dlc->socket, NULL, 0) < 0) {
				if (errno != EAGAIN) {
					report_call(call, LOG_ERR,
						"connect: %s\n",
						strerror(errno));
				}
			}
		}
	break;

	case U0_NULL_STATE:
		// Do nothing
	break;

	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
		q931_call_stop_any_timer(call);
		q931_channel_release(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_release_reference(call);

		Q931_DECLARE_IES(ies);
		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_NETWORK_OUT_OF_ORDER;
		q931_ies_add_put(&ies, &cause->ie);

		q931_call_primitive(call, Q931_CCB_RELEASE_INDICATION, &ies);

		Q931_UNDECLARE_IES(ies);
	break;

	case U10_ACTIVE:
		if (q931_call_timer_running(call, T309)) {
			q931_call_stop_timer(call, T309);
			q931_channel_release(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_release_reference(call);

			Q931_DECLARE_IES(ies);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value = Q931_IE_C_CV_NETWORK_OUT_OF_ORDER;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_primitive(call,
				Q931_CCB_RELEASE_INDICATION, &ies);

			Q931_UNDECLARE_IES(ies);
		} else {
			q931_call_start_timer(call, T309);

			if (connect(call->dlc->socket, NULL, 0) < 0) {
				if (errno != EAGAIN) {
					report_call(call, LOG_WARNING,
						"Cannot reconnect: %s\n",
						strerror(errno));
				}
			}
		}
	break;
	}
}

void q931_call_dl_release_confirm(struct q931_call *call)
{
	assert(call);
}

void q931_dispatch_message(
	struct q931_call *call,
	struct q931_message *msg)
{
	assert(call);
	assert(msg);
	assert(msg->dlc);

	report_call(call, LOG_DEBUG, "Got %s\n",
		q931_message_type_to_text(msg->message_type));

	switch (msg->message_type) {
	case Q931_MT_ALERTING:
		q931_handle_alerting(call, msg);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_handle_call_proceeding(call, msg);
	break;

	case Q931_MT_CONNECT:
		q931_handle_connect(call, msg);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
		q931_handle_connect_acknowledge(call, msg);
	break;

	case Q931_MT_PROGRESS:
		q931_handle_progress(call, msg);
	break;

	case Q931_MT_SETUP:
		q931_handle_setup(call, msg);
	break;

	case Q931_MT_SETUP_ACKNOWLEDGE:
		q931_handle_setup_acknowledge(call, msg);
	break;

	case Q931_MT_DISCONNECT:
		q931_handle_disconnect(call, msg);
	break;

	case Q931_MT_RELEASE:
		q931_handle_release(call, msg);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_handle_release_complete(call, msg);
	break;

	case Q931_MT_STATUS:
		q931_handle_status(call, msg);
	break;

	case Q931_MT_STATUS_ENQUIRY:
		q931_handle_status_enquiry(call, msg);
	break;

#if 0
	case Q931_MT_USER_INFORMATION:
		q931_handle_user_information(call, msg);
	break;

	case Q931_MT_SEGMENT:
		q931_handle_segment(call, msg);
	break;

	case Q931_MT_CONGESTION_CONTROL:
		q931_handle_congestion_control(call, msg);
	break;
#endif

	case Q931_MT_INFORMATION:
		q931_handle_info(call, msg);
	break;

/*	case Q931_MT_FACILITY:
		q931_handle_facility(call, msg);
	break;

	case Q931_MT_REGISTER:
		q931_handle_register(call, msg);
	break;
	*/

	case Q931_MT_NOTIFY:
		q931_handle_notify(call, msg);
	break;


/*	case Q931_MT_HOLD:
		q931_handle_hold(call, msg);
	break;

	case Q931_MT_HOLD_ACKNOWLEDGE:
		q931_handle_hold_acknowledge(call, msg);
	break;

	case Q931_MT_HOLD_REJECT:
		q931_handle_hold_reject(call, msg);
	break;

	case Q931_MT_RETRIEVE:
		q931_handle_retrieve(call, msg);
	break;

	case Q931_MT_RETRIEVE_ACKNOWLEDGE:
		q931_handle_retrieve_acknowledge(call, msg);
	break;

	case Q931_MT_RETRIEVE_REJECT:
		q931_handle_retrieve_reject(call, msg);
	break;*/

	case Q931_MT_RESUME:
		q931_handle_resume(call, msg);
	break;

	case Q931_MT_RESUME_ACKNOWLEDGE:
		q931_handle_resume_acknowledge(call, msg);
	break;

	case Q931_MT_RESUME_REJECT:
		q931_handle_resume_reject(call, msg);
	break;

	case Q931_MT_SUSPEND:
		q931_handle_suspend(call, msg);
	break;

	case Q931_MT_SUSPEND_ACKNOWLEDGE:
		q931_handle_suspend_acknowledge(call, msg);
	break;

	case Q931_MT_SUSPEND_REJECT:
		q931_handle_suspend_reject(call, msg);
	break;

	default: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
		q931_ies_add_put(&ies, &cause->ie);

		if (call->state == N0_NULL_STATE) {
			q931_call_send_release(call, &ies);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		} if (call->state == U0_NULL_STATE) {
			q931_call_send_release(call, &ies);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, U19_RELEASE_REQUEST);
		} else {
			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_call_status(call, &ies);

			Q931_UNDECLARE_IES(ies);
		}

		Q931_UNDECLARE_IES(ies);

		report_call(call, LOG_WARNING,
			"Unrecognized message %d in state %s\n",
			msg->message_type,
			q931_call_state_to_text(call->state));
	}
	break;
	}
}

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

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/ces.h>
#include <libq931/input.h>
#include <libq931/output.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/proto.h>

#include <libq931/ie_call_state.h>
#include <libq931/ie_cause.h>

#include "ces_inline.h"

const char *q931_ces_state_to_text(enum q931_call_state state)
{
	switch (state) {
	case U0_NULL_STATE:
		return "I0_NULL_STATE";
	case I7_CALL_RECEIVED:
		return "I7_CALL_RECEIVED";
	case I8_CONNECT_REQUEST:
		return "I8_CONNECT_REQUEST";
	case I9_INCOMING_CALL_PROCEEDING:
		return "I9_INCOMING_CALL_PROCEEDING";
	case I19_RELEASE_REQUEST:
		return "I19_RELEASE_REQUEST";
	case I25_OVERLAP_RECEIVING:
		return "I25_OVERLAP_RECEIVING";
	default: return "*UNKNOWN*";
	}
}

static void q931_ces_set_state(
	struct q931_ces *ces,
	enum q931_ces_state state)
{
	report_ces(ces, LOG_DEBUG,
		"%s ==to==> %s\n",
		q931_ces_state_to_text(ces->state),
		q931_ces_state_to_text(state));

	ces->state = state;
}

static void q931_ces_timer_T304(void *data);
static void q931_ces_timer_T308(void *data);
static void q931_ces_timer_T322(void *data);

struct q931_ces *q931_ces_alloc(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	struct q931_ces *ces;

	assert(call);
	assert(dlc);

	ces = malloc(sizeof(*ces));
	if (!ces)
		return NULL;

	q931_call_get(call);
	ces->call = call;
	ces->state = I0_NULL_STATE;
	ces->dlc = q931_dlc_get(dlc);

	q931_init_timer(&ces->T304, "T304", q931_ces_timer_T304, ces);
	q931_init_timer(&ces->T308, "T308", q931_ces_timer_T308, ces);
	q931_init_timer(&ces->T322, "T322", q931_ces_timer_T322, ces);

	report_ces(ces, LOG_DEBUG, "CES (TEI=%d) allocated for call %d\n",
		ces->dlc->tei,
		call->call_reference);

	list_add(&ces->node, &call->ces);

	return ces;
}

void q931_ces_free(struct q931_ces *ces)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES %d freed for call %d.%c\n",
		ces->dlc->tei,
		ces->call->call_reference,
		ces->call->direction ==
			Q931_CALL_DIRECTION_OUTBOUND ? 'O' : 'I');

	list_del(&ces->node);

	q931_dlc_put(ces->dlc);
	ces->dlc = NULL;
	
	q931_call_put(ces->call);
	ces->call = NULL;

	free(ces);
}

static void q931_ces_stop_any_timer(struct q931_ces *ces)
{
	q931_ces_stop_timer(ces, T304);
	q931_ces_stop_timer(ces, T308);
	q931_ces_stop_timer(ces, T322);
}

void q931_ces_dl_establish_indication(struct q931_ces *ces)
{
	assert(ces);

	if (ces->state == I25_OVERLAP_RECEIVING) {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(ces->call);
		cause->value = Q931_IE_C_CV_TEMPORARY_FAILURE;

		q931_ces_stop_timer(ces, T304);
		q931_ces_send_release(ces, &ies);
		q931_ces_start_timer(ces, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
		q931_int_release_indication(ces->call, ces, NULL);

		Q931_UNDECLARE_IES(ies);
	}
}

void q931_ces_dl_establish_confirm(struct q931_ces *ces)
{
}

void q931_ces_dl_release_indication(struct q931_ces *ces)
{
	assert(ces);

	if (ces->state != I0_NULL_STATE) {
		q931_ces_stop_any_timer(ces);
		q931_int_release_indication(ces->call, ces, NULL);
		q931_int_release_complete_indication(ces->call, ces, NULL);
	}
}

void q931_ces_dl_release_confirm(struct q931_ces *ces)
{
}

static __u8 q931_ces_state_to_ie_state(
		enum q931_call_state state)
{
	switch (state) {
	case I0_NULL_STATE:
		return Q931_IE_CS_N0_NULL_STATE;
	case I7_CALL_RECEIVED:
		return Q931_IE_CS_N7_CALL_RECEIVED;
	case I8_CONNECT_REQUEST:
		return Q931_IE_CS_N8_CONNECT_REQUEST;
	case I9_INCOMING_CALL_PROCEEDING:
		return Q931_IE_CS_N9_INCOMING_CALL_PROCEEDING;
	case I19_RELEASE_REQUEST:
		return Q931_IE_CS_N19_RELEASE_REQUEST;
	case I25_OVERLAP_RECEIVING:
		return Q931_IE_CS_N25_OVERLAP_RECEIVING;
	default:
		assert(0);
		return 0;
	}
}

static inline void q931_ces_send_ces_status(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	Q931_DECLARE_IES(ies);

	q931_ies_merge(&ies, user_ies);

	struct q931_ie_call_state *cs = q931_ie_call_state_alloc();
	cs->coding_standard = Q931_IE_CS_CS_CCITT;
	cs->value = q931_ces_state_to_ie_state(ces->state),
	q931_ies_add_put(&ies, &cs->ie);

	q931_ces_send_status(ces, &ies);

	Q931_UNDECLARE_IES(ies);
}

#define q931_ces_unexpected_timer(call)	\
	_q931_ces_unexpected_timer((call), __FUNCTION__)

static void _q931_ces_unexpected_timer(
	struct q931_ces *ces,
	const char *event)
{
	report_ces(ces, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_ces_state_to_text(ces->state));
}
#define q931_ces_unexpected_primitive(call)	\
	_q931_ces_unexpected_primitive((call), __FUNCTION__)

static void _q931_ces_unexpected_primitive(
	struct q931_ces *ces,
	const char *event)
{
	report_ces(ces, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_ces_state_to_text(ces->state));
}

#define q931_ces_message_not_compatible_with_state(call, msg)	\
	_q931_ces_message_not_compatible_with_state((call), (msg), __FUNCTION__)

static void _q931_ces_message_not_compatible_with_state(
	struct q931_ces *ces,
	const struct q931_message *msg,
	const char *event)
{
	switch (ces->state) {
	case I0_NULL_STATE:
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING: {
		Q931_DECLARE_IES(ies);
		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(ces->call);
		cause->value = Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
		memcpy(&cause->diagnostics,
			msg->raw + sizeof(struct q931_header) +
				msg->callref_len,
			1);
		cause->diagnostics_len = 1;
		q931_ies_add_put(&ies, &cause->ie);

		q931_ces_send_ces_status(ces, &ies);
		Q931_UNDECLARE_IES(ies);
	}
	break;
	}

	report_ces(ces, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_ces_state_to_text(ces->state));
}


void q931_ces_alerting_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-ALERTING-REQ\n");

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I7_CALL_RECEIVED);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

void q931_ces_connect_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-CONNECT-REQ\n");

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

void q931_ces_call_proceeding_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-PROCEEDING-REQ\n");

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I9_INCOMING_CALL_PROCEEDING);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

void q931_ces_setup_ack_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-SETUP-ACK-REQ\n");

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_start_timer(ces, T304);
		q931_ces_set_state(ces, I25_OVERLAP_RECEIVING);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

void q931_ces_release_request(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-RELEASE-REQ\n");

	switch (ces->state) {
	case I0_NULL_STATE:
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		q931_ces_start_timer(ces, T308);
		q931_ces_send_release(ces, ies);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_ces_start_timer(ces, T308);
		q931_ces_send_release(ces, ies);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_ces_stop_timer(ces, T304);
		q931_ces_start_timer(ces, T308);
		q931_ces_send_release(ces, ies);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I19_RELEASE_REQUEST:
		q931_ces_unexpected_primitive(ces);
	break;

	}
}

void q931_ces_info_request(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-INFO-REQ\n");

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		q931_ces_send_information(ces, ies);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_ces_stop_timer(ces, T304);
		q931_ces_send_information(ces, ies);
		q931_ces_start_timer(ces, T304);
	break;

	case I0_NULL_STATE:
	case I19_RELEASE_REQUEST:
	default:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

void q931_ces_status_enquiry_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES-STATUS-ENQ-REQ\n");

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I25_OVERLAP_RECEIVING:
	case I19_RELEASE_REQUEST:
		if (!q931_call_timer_running(ces->call, T322)) {
			q931_ces_send_status_enquiry(ces, user_ies);
			q931_ces_start_timer(ces, T322);
		}
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_unexpected_primitive(ces);
	break;
	}
}

static inline void q931_ces_handle_alerting(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_set_state(ces, I7_CALL_RECEIVED);
		q931_int_alerting_indication(ces->call, ces, &msg->ies);
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_ces_set_state(ces, I7_CALL_RECEIVED);
		q931_int_alerting_indication(ces->call, ces, &msg->ies);
	break;

	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_call_proceeding(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_ces_set_state(ces, I9_INCOMING_CALL_PROCEEDING);
		q931_int_call_proceeding_indication(ces->call, ces, &msg->ies);
	break;

	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_connect(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
		q931_int_connect_indication(ces->call, ces, &msg->ies);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
		q931_int_connect_indication(ces->call, ces, &msg->ies);
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
		q931_int_connect_indication(ces->call, ces, &msg->ies);
	break;

	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_progress(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_int_progress_indication(ces->call, ces, &msg->ies);
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_int_progress_indication(ces->call, ces, &msg->ies);
	break;

	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_disconnect(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_send_release(ces, NULL);
		q931_ces_start_timer(ces, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
		q931_int_release_indication(ces->call, ces, &msg->ies);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_send_release(ces, NULL);
		q931_ces_start_timer(ces, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
		q931_int_release_indication(ces->call, ces, &msg->ies);
	break;

	case I19_RELEASE_REQUEST:
		// Do nothing
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_ces_send_release(ces, NULL);
		q931_ces_start_timer(ces, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
		q931_int_release_indication(ces->call, ces, &msg->ies);
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_release(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_send_release_complete(ces, NULL);
		q931_int_release_indication(ces->call, ces, &msg->ies);
		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I19_RELEASE_REQUEST:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T308);
		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_ces_send_release_complete(ces, NULL);
		q931_int_release_indication(ces->call, ces, &msg->ies);
		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_release_complete(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_int_release_indication(ces->call, ces, &msg->ies);
		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I19_RELEASE_REQUEST:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T308);
		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I25_OVERLAP_RECEIVING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_ces_stop_timer(ces, T304);
		q931_int_release_indication(ces->call, ces, &msg->ies);

		// Implementation dependend
		/*
		q931_ces_stop_timer(ces, T304);
		q931_send_release(ces->dlc);

		q931_ces_start_timer(ces, T308);
		*/

		q931_int_release_complete_indication(ces->call, ces, &msg->ies);
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_status(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I25_OVERLAP_RECEIVING: {
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;
		struct q931_ie_cause *cause = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			} else if (msg->ies.ies[i]->cls->id == Q931_IE_CAUSE) {
				cause = container_of(msg->ies.ies[i],
					struct q931_ie_cause, ie);
			}
		}

		if (q931_timer_pending(&ces->call->T322)) {
			if (cause->value ==
				Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED) {
				// Save cause and call state (!?!?!?)
				return;
			} else if(cause->value ==
				Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY) {
				q931_ces_stop_timer(ces, T322);
			}
		}

		if (cs && cs->value == q931_ces_state_to_ie_state(I0_NULL_STATE)) {
			q931_ces_stop_any_timer(ces);
			q931_int_release_indication(ces->call, ces, &msg->ies);
			q931_int_release_complete_indication(ces->call, ces, &msg->ies);
		} else if (cs && cs->value == q931_ces_state_to_ie_state(ces->state)) {
				// NOTE 1. FURTHER ACTIONS ARE AN IMPLEMENTATION OPTION.
		} else {
			q931_ces_stop_any_timer(ces);

			Q931_DECLARE_IES(ies);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(ces->call);
			cause->value = Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_ces_send_release(ces, &ies);

			q931_ces_start_timer(ces, T308);
			q931_ces_set_state(ces, I19_RELEASE_REQUEST);
			q931_call_primitive1(ces->call, Q931_CCB_STATUS_INDICATION,
				&msg->ies, Q931_STATUS_INDICATION_ERROR);
			q931_int_release_indication(ces->call, ces, &msg->ies);
			Q931_UNDECLARE_IES(ies);
		}
	}
	break;

	case I19_RELEASE_REQUEST: {
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		struct q931_ie_call_state *cs = NULL;

		int i;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == Q931_IE_CALL_STATE) {
				cs = container_of(msg->ies.ies[i],
					struct q931_ie_call_state, ie);
			}
		}

		if (cs && cs->value == q931_ces_state_to_ie_state(I0_NULL_STATE)) {
			q931_ces_stop_timer(ces, T308);
			q931_int_release_complete_indication(ces->call, ces, &msg->ies);
		}
	}
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_info(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		if (q931_decode_information_elements(ces->call, msg) < 0)
			break;

		q931_int_info_indication(ces->call, ces, &msg->ies);
	break;

	case I19_RELEASE_REQUEST:
		// Do nothing
		// NOTE 1. THE INDIVIDUAL PROCESS MAY PASS AN INT. INFO
		//	 INDICATION TO THE GLOBAL PROCESS AT THIS POINT.
		//	 (REF. SECTION 5.0)

	break;

	case I25_OVERLAP_RECEIVING:
	break;

	case I0_NULL_STATE:
	default:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;
	}
}

static inline void q931_ces_handle_status_enquiry(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	assert(ces);
	assert(msg);

	Q931_DECLARE_IES(ies);
	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(ces->call);
	cause->value = Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY;
	q931_ies_add_put(&ies, &cause->ie);

	q931_ces_send_status(ces, &ies);
	Q931_UNDECLARE_IES(ies);
}

void q931_ces_timer_T304(void *data)
{
	struct q931_ces *ces = data;
	assert(ces);

	if (ces->state != I25_OVERLAP_RECEIVING) {
		q931_ces_unexpected_timer(ces);
		return;
	}

	Q931_DECLARE_IES(ies);
	struct q931_ie_cause *cause = q931_ie_cause_alloc();
	cause->coding_standard = Q931_IE_C_CS_CCITT;
	cause->location = q931_ie_cause_location_call(ces->call);
	cause->value = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
	memcpy(cause->diagnostics, "304", 3);
	cause->diagnostics_len = 3;
	q931_ies_add_put(&ies, &cause->ie);

	q931_ces_send_release(ces, &ies);
	q931_ces_start_timer(ces, T308);
	q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	q931_int_release_indication(ces->call, ces, NULL);
	Q931_UNDECLARE_IES(ies);
}

void q931_ces_timer_T308(void *data)
{
	struct q931_ces *ces = data;
	assert(ces);

	if (ces->state != I19_RELEASE_REQUEST) {
		q931_ces_unexpected_timer(ces);
		return;
	}

	if (!ces->T308_fired) {
		q931_ces_send_release(ces, NULL);
		q931_ces_start_timer(ces, T308);
		ces->T308_fired = TRUE;
	} else {
		q931_int_release_complete_indication(ces->call, ces, NULL);
	}
}

void q931_ces_timer_T322(void *data)
{
	struct q931_ces *ces = data;
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		if (ces->senq_cause_97_98_received) {
			// Implementation dependent
		} else {
			if (ces->senq_cnt > 3) {
				Q931_DECLARE_IES(ies);
				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(ces->call);
				cause->value = Q931_IE_C_CV_TEMPORARY_FAILURE;
				q931_ies_add_put(&ies, &cause->ie);

				q931_ces_send_release(ces, &ies);
				q931_ces_start_timer(ces, T308);
				q931_int_release_indication(ces->call, ces, NULL);
				Q931_UNDECLARE_IES(ies);
			} else {
				q931_ces_send_status_enquiry(ces, NULL);
				q931_ces_start_timer(ces, T322);
				ces->senq_cnt++;
			}
		}
	break;

	case I0_NULL_STATE:
		q931_ces_unexpected_timer(ces);
	break;
	}
}

void q931_ces_dispatch_message(
	struct q931_ces *ces,
	struct q931_message *msg)
{
	report_ces(ces, LOG_DEBUG, "Got %s\n",
		q931_message_type_to_text(msg->message_type));

	switch (msg->message_type) {
	case Q931_MT_ALERTING:
		q931_ces_handle_alerting(ces, msg);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_ces_handle_call_proceeding(ces, msg);
	break;

	case Q931_MT_CONNECT:
		q931_ces_handle_connect(ces, msg);
	break;

	case Q931_MT_PROGRESS:
		q931_ces_handle_progress(ces, msg);
	break;

	case Q931_MT_DISCONNECT:
		q931_ces_handle_disconnect(ces, msg);
	break;

	case Q931_MT_RELEASE:
		q931_ces_handle_release(ces, msg);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_ces_handle_release_complete(ces, msg);
	break;

	case Q931_MT_STATUS:
		q931_ces_handle_status(ces, msg);
	break;

	case Q931_MT_INFORMATION:
		q931_ces_handle_info(ces, msg);
	break;

	case Q931_MT_STATUS_ENQUIRY:
		q931_ces_handle_status_enquiry(ces, msg);
	break;

	case Q931_MT_SETUP:
		// Do nothing
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
	case Q931_MT_SETUP_ACKNOWLEDGE:
	case Q931_MT_RESTART:
	case Q931_MT_RESTART_ACKNOWLEDGE:
	case Q931_MT_USER_INFORMATION:
	case Q931_MT_SEGMENT:
	case Q931_MT_CONGESTION_CONTROL:
	case Q931_MT_FACILITY:
	case Q931_MT_NOTIFY:
	case Q931_MT_HOLD:
	case Q931_MT_HOLD_ACKNOWLEDGE:
	case Q931_MT_HOLD_REJECT:
	case Q931_MT_RETRIEVE:
	case Q931_MT_RETRIEVE_ACKNOWLEDGE:
	case Q931_MT_RETRIEVE_REJECT:
	case Q931_MT_RESUME:
	case Q931_MT_RESUME_ACKNOWLEDGE:
	case Q931_MT_RESUME_REJECT:
	case Q931_MT_SUSPEND:
	case Q931_MT_SUSPEND_ACKNOWLEDGE:
	case Q931_MT_SUSPEND_REJECT:
		q931_ces_message_not_compatible_with_state(ces, msg);
	break;

	default:
		report_ces(ces, LOG_WARNING,
			"Unkwnon/unhandled message type %d\n",
			msg->message_type);
	break;
	}
}

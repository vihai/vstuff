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

#include "q931.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "ces.h"
#include "out.h"
#include "call.h"
#include "intf.h"

static const char *q931_ces_state_to_text(enum q931_call_state state)
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
		"CES %d changed state from %s to %s\n",
		ces->dlc->tei,
		q931_ces_state_to_text(ces->state),
		q931_ces_state_to_text(state));

	ces->state = state;
}

//static void q931_ces_timer_T304(void *data);

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

	ces->call = call;
	ces->state = I0_NULL_STATE;
	ces->dlc = dlc;

//	q931_init_timer(&call->T304, q931_ces_timer_T304, call);

	report_ces(ces, LOG_DEBUG, "CES (TEI=%d) allocated for call %d\n",
		ces->dlc->tei,
		call->call_reference);

	list_add(&ces->node, &call->ces);

	return ces;
}

void q931_ces_free(struct q931_ces *ces)
{
	assert(ces);

	report_ces(ces, LOG_DEBUG, "CES freed for call %d\n",
		ces->call->call_reference);

	list_del(&ces->node);

	free(ces);
}

void q931_ces_dl_establish_indication(struct q931_ces *ces)
{
	assert(ces);

	if (ces->state == I25_OVERLAP_RECEIVING) {
		q931_call_stop_timer(ces->call, T304);
		q931_int_release_indication(ces->call, ces);
		q931_send_release_cause(ces->call, ces->dlc,
			Q931_IE_C_CV_TEMPORARY_FAILURE);
		q931_call_start_timer(ces->call, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	}
}

void q931_ces_dl_establish_confirm(struct q931_ces *ces)
{
}

void q931_ces_dl_release_indication(struct q931_ces *ces)
{
	assert(ces);

	if (ces->state != I0_NULL_STATE) {
		q931_call_stop_timer(ces->call, T304);
		q931_call_stop_timer(ces->call, T308);
		q931_call_stop_timer(ces->call, T310);
		q931_int_release_indication(ces->call, ces);
		q931_int_release_complete_indication(ces->call, ces);
	}
}

void q931_ces_dl_release_confirm(struct q931_ces *ces)
{
}

#define q931_ces_unexpected_state(call)	\
	_q931_ces_unexpected_state((call), __FUNCTION__)

static void _q931_ces_unexpected_state(
	struct q931_ces *ces,
	const char *event)
{
	q931_send_status(ces->call, ces->dlc,
		Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);

	report_ces(ces, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_ces_state_to_text(ces->state));
}

void q931_ces_alerting_request(struct q931_ces *ces)
{
	assert(ces);

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I7_CALL_RECEIVED);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

void q931_ces_connect_request(struct q931_ces *ces)
{
	assert(ces);

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

void q931_ces_call_proceeding_request(struct q931_ces *ces)
{
	assert(ces);

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_ces_set_state(ces, I9_INCOMING_CALL_PROCEEDING);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

void q931_ces_setup_ack_request(struct q931_ces *ces)
{
	assert(ces);

	switch (ces->state) {
	case I0_NULL_STATE:
		q931_call_start_timer(ces->call, T304);
		q931_ces_set_state(ces, I25_OVERLAP_RECEIVING);
	break;

	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

void q931_ces_release_request(struct q931_ces *ces,
	enum q931_ie_cause_value cause)
{
	assert(ces);

	switch (ces->state) {
	case I0_NULL_STATE:
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		q931_call_start_timer(ces->call, T308);
		q931_send_release_cause(ces->call, ces->dlc, cause);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_call_start_timer(ces->call, T308);
		q931_send_release_cause(ces->call, ces->dlc, cause);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I19_RELEASE_REQUEST:
		q931_send_status(ces->call, ces->dlc,
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_call_start_timer(ces->call, T308);
		q931_send_release_cause(ces->call, ces->dlc, cause);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;
	}
}

void q931_ces_info_request(struct q931_ces *ces)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		q931_send_info(ces->call, ces->dlc);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_send_info(ces->call, ces->dlc);
		q931_call_start_timer(ces->call, T304);
	break;

	case I0_NULL_STATE:
	case I19_RELEASE_REQUEST:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_alerting(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I19_RELEASE_REQUEST:
		q931_send_status(ces->call, ces->dlc,
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_alerting_indication(ces->call, ces);
		q931_ces_set_state(ces, I7_CALL_RECEIVED);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_alerting_indication(ces->call, ces);
		q931_ces_set_state(ces, I7_CALL_RECEIVED);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_call_proceeding(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
		q931_send_status(ces->call, ces->dlc,
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_call_proceeding_indication(ces->call, ces);
		q931_call_start_timer(ces->call, T310);
		q931_ces_set_state(ces, I9_INCOMING_CALL_PROCEEDING);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_connect(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
		q931_int_connect_indication(ces->call, ces);

		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
	break;

	case I8_CONNECT_REQUEST:
	case I19_RELEASE_REQUEST:
		q931_send_status(ces->call, ces->dlc,
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_connect_indication(ces->call, ces);
		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_connect_indication(ces->call, ces);
		q931_ces_set_state(ces, I8_CONNECT_REQUEST);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_progress(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
		q931_int_progress_indication(ces->call, ces);
	break;

	case I8_CONNECT_REQUEST:
	case I19_RELEASE_REQUEST:
		q931_send_status(ces->call, ces->dlc,
			Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_progress_indication(ces->call, ces);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_progress_indication(ces->call, ces);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_disconnect(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);
		q931_send_release(ces->call, ces->dlc);
		q931_call_start_timer(ces->call, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_release_indication(ces->call, ces);
		q931_send_release(ces->call, ces->dlc);
		q931_call_start_timer(ces->call, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I19_RELEASE_REQUEST:
		// Do nothing
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_release_indication(ces->call, ces);
		q931_send_release(ces->call, ces->dlc);
		q931_call_start_timer(ces->call, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_release(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);
		q931_send_release_complete(ces->call, ces->dlc);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_release_indication(ces->call, ces);
		q931_send_release_complete(ces->call, ces->dlc);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I19_RELEASE_REQUEST:
		q931_call_stop_timer(ces->call, T308);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_release_indication(ces->call, ces);
		q931_send_release_complete(ces->call, ces->dlc);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_release_complete(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	assert(ces);

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(ces->call, T310);
		q931_int_release_indication(ces->call, ces);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I19_RELEASE_REQUEST:
		q931_call_stop_timer(ces->call, T308);
		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I25_OVERLAP_RECEIVING:
		q931_call_stop_timer(ces->call, T304);
		q931_int_release_indication(ces->call, ces);

		// Implementation dependend
		/*
		q931_call_stop_timer(ces->call, T304);
		q931_send_release(ces->dlc);

		q931_call_start_timer(ces->call, T308);
		*/

		q931_int_release_complete_indication(ces->call, ces);
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_status(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Complete implementation

	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
	case I19_RELEASE_REQUEST:
	case I25_OVERLAP_RECEIVING:
		// Log error?
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

inline static void q931_ces_handle_info(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I9_INCOMING_CALL_PROCEEDING:
		q931_int_info_indication(ces->call, ces);
	break;

	case I19_RELEASE_REQUEST:
		// Do nothing
		// NOTE 1. THE INDIVIDUAL PROCESS MAY PASS AN INT. INFO
		//         INDICATION TO THE GLOBAL PROCESS AT THIS POINT.
		//         (REF. SECTION 5.0)

	break;

	case I25_OVERLAP_RECEIVING:
	break;

	case I0_NULL_STATE:
	default:
		report_ces(ces, LOG_ERR,
			"Unexpected  in state %s\n",
			q931_ces_state_to_text(ces->state));
	break;
	}
}

//void q931_ces_timer_T304(void *data)
void q931_ces_timer_T304(struct q931_ces *ces)
{
	if (ces->state != I25_OVERLAP_RECEIVING) {
		q931_ces_unexpected_state(ces);
		return;
	}

	q931_int_release_indication(ces->call, ces);
	q931_send_release(ces->call, ces->dlc);
	q931_call_start_timer(ces->call, T308);
	q931_ces_set_state(ces, I19_RELEASE_REQUEST);
}

void q931_ces_timer_T310(struct q931_ces *ces)
{
	switch (ces->state) {
	case I9_INCOMING_CALL_PROCEEDING:
		q931_int_release_indication(ces->call, ces);
		q931_send_release(ces->call, ces->dlc);
		q931_call_start_timer(ces->call, T308);
		q931_ces_set_state(ces, I19_RELEASE_REQUEST);
	break;
	case I19_RELEASE_REQUEST:
		// First expiry?
		if (1) {
			q931_send_release(ces->call, ces->dlc);
			q931_call_start_timer(ces->call, T308);
		} else {
			q931_int_release_complete_indication(ces->call, ces);
		}
	break;

	case I0_NULL_STATE:
	case I7_CALL_RECEIVED:
	case I8_CONNECT_REQUEST:
	case I25_OVERLAP_RECEIVING:
		q931_ces_unexpected_state(ces);
	break;
	}
}

void q931_ces_dispatch_message(
	struct q931_ces *ces,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt)
{
	report_ces(ces, LOG_DEBUG, "%s dispatched to CES %d\n",
		q931_message_type_to_text(message_type),
		ces->dlc->tei);

	switch (message_type) {
	case Q931_MT_ALERTING:
		q931_ces_handle_alerting(ces, ies, ies_cnt);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_ces_handle_call_proceeding(ces, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT:
		q931_ces_handle_connect(ces, ies, ies_cnt);
	break;

	case Q931_MT_PROGRESS:
		q931_ces_handle_progress(ces, ies, ies_cnt);
	break;

	case Q931_MT_DISCONNECT:
		q931_ces_handle_disconnect(ces, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE:
		q931_ces_handle_release(ces, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_ces_handle_release_complete(ces, ies, ies_cnt);
	break;

	case Q931_MT_STATUS:
		q931_ces_handle_status(ces, ies, ies_cnt);
	break;

	case Q931_MT_INFORMATION:
		q931_ces_handle_info(ces, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
	case Q931_MT_SETUP:
	case Q931_MT_SETUP_ACKNOWLEDGE:
	case Q931_MT_RESTART:
	case Q931_MT_RESTART_ACKNOWLEDGE:
	case Q931_MT_STATUS_ENQUIRY:
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
		report_ces(ces, LOG_DEBUG,
			"Unexpected message %s\n",
			q931_message_type_to_text(message_type));
	break;

	default:
		report_ces(ces, LOG_WARNING,
			"Unkwnon/unhandled message type %d\n",
			message_type);
	break;
	}
}

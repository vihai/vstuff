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

#include <lapd.h>

#include "list.h"

#include "q931.h"
#include "q931_log.h"
#include "q931_mt.h"
#include "q931_ie.h"

struct q931_ces *q931_ces_alloc(struct q931_call *call)
{
	struct q931_ces *ces;

	ces = malloc(sizeof(struct q931_ces));
	if (!ces)
		return NULL;

	ces->call = call;
	ces->state = N0_NULL_STATE;
}

void q931_ces_free(struct q931_ces *ces)
{
	free(ces);
}

void q931_ces_alerting_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N0_NULL_STATE:
		ces->state = N7_CALL_RECEIVED;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_ces_connect_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N0_NULL_STATE:
		ces->state = N8_CONNECT_REQUEST;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_ces_call_proceeding_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N0_NULL_STATE:
		// Start T310
		ces->state = N25_OVERLAP_RECEIVING;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_ces_setup_ack_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N0_NULL_STATE:
		// Start T304

		ces->state = N25_OVERLAP_RECEIVING;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

}

void q931_ces_release_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N0_NULL_STATE:
		// Start T304

		q931_send_release(ces->dlc);

		ces->state = N19_RELEASE_REQUEST;
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_send_release(ces->dlc);

		// Start T308

		ces_state = N19_RELEASE_REQUEST;
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_send_release(ces->dlc);

		// Start T308

		ces_state = N19_RELEASE_REQUEST;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_ces_info_request(struct q931_ces *ces)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_info(ces->dlc);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_alerting(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_send_status(ces->call, ces);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_int_alert_indication(ces->call, ces);

		ces->state = N7_CALL_RECEIVED;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_call_proceeding(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_connect(
	const struct q931_dlc *dlc,
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
		q931_int_connect_indication(ces->call, ces);

		ces->state = N8_CONNECT_REQUEST;
	break;

	case N8_CONNECT_REQUEST:
		q931_send_status(ces->call, ces);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_int_connect_indication(ces->call, ces);

		ces->state = N8_CONNECT_REQUEST;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_connect_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_progress(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
		q931_int_progress_indication(ces->call, ces);
	break;

	case N8_CONNECT_REQUEST:
		q931_send_status(ces->call, ces);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_int_progress_indication(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_setup(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_setup_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_disconnect(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);

		q931_send_release(ces->dlc);

		// Start T308

		ces->state = N19_RELEASE_REQUEST;
	break;

	case N9_INCOMING_CALL_PROCEEDING:

		// Stop T310 if running

		q931_int_release_indication(ces->call, ces);

		q931_send_release(ces->dlc);

		// Start T308
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_release(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);

		q931_send_release_complete(ces->dlc);

		q931_int_release_complete_indication(ces->call, ces);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_int_release_indication(ces->call, ces);

		q931_send_release_complete(ces->dlc);

		q931_int_release_complete_indication(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_release_complete(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_int_release_indication(ces->call, ces);

		q931_int_release_complete_indication(ces->call, ces);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		q931_int_release_indication(ces->call, ces);

		q931_int_release_complete_indication(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_restart(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_restart_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_status(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		// Log error?
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_status_enquiry(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_user_information(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
	
}

inline static void q931_ces_handle_segment(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_congestion_control(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_info(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_int_info_indication(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_facility(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_notify(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_hold(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_hold_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_hold_reject(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_retrieve(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_retrieve_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_retrieve_reject(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_resume(
	const struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_resume_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_resume_reject(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_suspend(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_suspend_acknowledge(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_ces_handle_suspend_reject(
	struct q931_ces *ces,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (ces->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_status(ces->call, ces);
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}


void q931_ces_dispatch_message(
	struct q931_call *call,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (message_type) {
	case Q931_MT_ALERTING:
		q931_ces_handle_alerting(ces, ies, ies_cnt);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_ces_handle_call_proceeding(ces, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT:
		q931_ces_handle_connect(dlc, call, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
		q931_ces_handle_connect_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_PROGRESS:
		q931_ces_handle_progress(ces, ies, ies_cnt);
	break;

	case Q931_MT_SETUP:
		q931_ces_handle_setup(ces, ies, ies_cnt);
	break;

	case Q931_MT_SETUP_ACKNOWLEDGE:
		q931_ces_handle_setup_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_DISCONNECT:
		q931_ces_handle_disconnect(ces, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE:
		q931_ces_handle_release(dlc, call, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_ces_handle_release_complete(dlc, call, ies, ies_cnt);
	break;

	case Q931_MT_RESTART:
		q931_ces_handle_restart(ces, ies, ies_cnt);
	break;

	case Q931_MT_RESTART_ACKNOWLEDGE:
		q931_ces_handle_restart_acknowledge(ces, ies, ies_cnt);
	break;


	case Q931_MT_STATUS:
		q931_ces_handle_status(ces, ies, ies_cnt);
	break;

	case Q931_MT_STATUS_ENQUIRY:
		q931_ces_handle_status_enquiry(ces, ies, ies_cnt);
	break;

	case Q931_MT_USER_INFORMATION:
		q931_ces_handle_user_information(ces, ies, ies_cnt);
	break;

	case Q931_MT_SEGMENT:
		q931_ces_handle_segment(ces, ies, ies_cnt);
	break;

	case Q931_MT_CONGESTION_CONTROL:
		q931_ces_handle_congestion_control(ces, ies, ies_cnt);
	break;

	case Q931_MT_INFORMATION:
		q931_ces_handle_info(ces, ies, ies_cnt);
	break;

	case Q931_MT_FACILITY:
		q931_ces_handle_facility(ces, ies, ies_cnt);
	break;

	case Q931_MT_NOTIFY:
		q931_ces_handle_notify(ces, ies, ies_cnt);
	break;


	case Q931_MT_HOLD:
		q931_ces_handle_hold(ces, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_ACKNOWLEDGE:
		q931_ces_handle_hold_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_REJECT:
		q931_ces_handle_hold_reject(ces, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE:
		q931_ces_handle_retrieve(ces, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_ACKNOWLEDGE:
		q931_ces_handle_retrieve_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_REJECT:
		q931_ces_handle_retrieve_reject(ces, ies, ies_cnt);
	break;

	case Q931_MT_RESUME:
		q931_ces_handle_resume(ces, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_ACKNOWLEDGE:
		q931_ces_handle_resume_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_REJECT:
		q931_ces_handle_resume_reject(ces, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND:
		q931_ces_handle_suspend(ces, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_ACKNOWLEDGE:
		q931_ces_handle_suspend_acknowledge(ces, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_REJECT:
		q931_ces_handle_suspend_reject(ces, ies, ies_cnt);
	break;

	default:
		report_dlc(dlc, LOG_WARNING,
			"Unkwnon/unhandled message type %d\n",
			message_type);
	break;
	}
}

void q931_ces_timer_T310()
{
	switch (ces->state) {
	case N9_INCOMING_CALL_PROCEEDING:
		q931_int_release_indication(ces->call, ces);

		q931_send_release(ces->dlc);

		// Start T308

		ces->state = N19_RELEASE_REQUEST;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

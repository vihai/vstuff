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

#define Q931_PRIVATE

#include "q931.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
#include "call.h"
#include "intf.h"
#include "ces.h"
#include "channel.h"

static void q931_timer_T301(void *data);
static void q931_timer_T302(void *data);
static void q931_timer_T303(void *data);
static void q931_timer_T304(void *data);
static void q931_timer_T305(void *data);
static void q931_timer_T306(void *data);
static void q931_timer_T307(void *data);
static void q931_timer_T308(void *data);
static void q931_timer_T309(void *data);
static void q931_timer_T310(void *data);
static void q931_timer_T312(void *data);
static void q931_timer_T313(void *data);
static void q931_timer_T314(void *data);
static void q931_timer_T316(void *data);
static void q931_timer_T317(void *data);
static void q931_timer_T318(void *data);
static void q931_timer_T319(void *data);
static void q931_timer_T320(void *data);
static void q931_timer_T321(void *data);
static void q931_timer_T322(void *data);

static const char *q931_state_to_text(enum q931_call_state state)
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

static void q931_call_set_state(
	struct q931_call *call,
	enum q931_call_state state)
{
	report_call(call, LOG_DEBUG,
		"Call %d changed state from %s to %s\n",
		call->call_reference,
		q931_state_to_text(call->state),
		q931_state_to_text(state));

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

struct q931_call *q931_alloc_call(struct q931_interface *interface)
{
	struct q931_call *call;

	call = malloc(sizeof(*call));
	if (!call) abort();
	memset(call, 0x00, sizeof(*call));

	strcpy(call->calling_number, "");
	strcpy(call->called_number, "");

	call->interface = interface;

	if (call->interface->role == LAPD_ROLE_TE)
		call->state = U0_NULL_STATE;
	else
		call->state = N0_NULL_STATE;

	INIT_LIST_HEAD(&call->ces);

	q931_init_timer(&call->T301, q931_timer_T301, call);
	q931_init_timer(&call->T302, q931_timer_T302, call);
	q931_init_timer(&call->T303, q931_timer_T303, call);
	q931_init_timer(&call->T304, q931_timer_T304, call);
	q931_init_timer(&call->T305, q931_timer_T305, call);
	q931_init_timer(&call->T306, q931_timer_T306, call);
	q931_init_timer(&call->T307, q931_timer_T307, call);
	q931_init_timer(&call->T308, q931_timer_T308, call);
	q931_init_timer(&call->T309, q931_timer_T309, call);
	q931_init_timer(&call->T310, q931_timer_T310, call);
	q931_init_timer(&call->T312, q931_timer_T312, call);
	q931_init_timer(&call->T313, q931_timer_T313, call);
	q931_init_timer(&call->T314, q931_timer_T314, call);
	q931_init_timer(&call->T316, q931_timer_T316, call);
	q931_init_timer(&call->T317, q931_timer_T317, call);
	q931_init_timer(&call->T318, q931_timer_T318, call);
	q931_init_timer(&call->T319, q931_timer_T319, call);
	q931_init_timer(&call->T320, q931_timer_T320, call);
	q931_init_timer(&call->T321, q931_timer_T321, call);
	q931_init_timer(&call->T322, q931_timer_T322, call);

	// Inherit callbacks from interface
	call->alerting_indication = interface->alerting_indication;
	call->connect_indication = interface->connect_indication;
	call->disconnect_indication = interface->disconnect_indication;
	call->error_indication = interface->error_indication;
	call->info_indication = interface->info_indication;
	call->more_info_indication = interface->more_info_indication;
	call->notify_indication = interface->notify_indication;
	call->proceeding_indication = interface->proceeding_indication;
	call->progress_indication = interface->progress_indication;
	call->reject_indication = interface->reject_indication;
	call->release_confirm = interface->release_confirm;
	call->release_indication = interface->release_indication;
	call->resume_confirm = interface->resume_confirm;
	call->resume_indication = interface->resume_indication;
	call->setup_complete_indication = interface->setup_complete_indication;
	call->setup_confirm = interface->setup_confirm;
	call->setup_indication = interface->setup_indication;
	call->status_indication = interface->status_indication;
	call->suspend_confirm = interface->suspend_confirm;
	call->suspend_indication = interface->suspend_indication;
	call->timeout_indication = interface->timeout_indication;

	return call;
}

struct q931_call *q931_alloc_call_in(
	struct q931_interface *interface,
	struct q931_dlc *dlc,
	int call_reference,
	int broadcast_setup)
{
	struct q931_call *call;
	call = q931_alloc_call(interface);
	if (!call)
		return NULL;

	if (broadcast_setup) {
		call->dlc = NULL;
		call->broadcast_setup = TRUE;
	} else {
		call->dlc = dlc;
		call->broadcast_setup = FALSE;
	}

	call->direction = Q931_CALL_DIRECTION_INBOUND;
	call->call_reference = call_reference;

	q931_intf_add_call(dlc->interface, call);

	return call;
}

struct q931_call *q931_alloc_call_out(
	struct q931_interface *interface)
{
	struct q931_call *call;
	call = q931_alloc_call(interface);
	if (!call)
		return NULL;

	call->direction = Q931_CALL_DIRECTION_OUTBOUND;
	call->call_reference =
		q931_alloc_call_reference(call->interface);

	if (call->call_reference < 0) {
		report_call(call, LOG_ERR,
			"Cannot find an available call reference number!\n");

		q931_free_call(call);
		return NULL;
	}

	if (interface->role == LAPD_ROLE_TE)
		call->dlc = &interface->dlc;
	else
		call->dlc = NULL;

	q931_intf_add_call(call->interface, call);

	return call;
}

void q931_free_call(struct q931_call *call)
{
	assert(call);
	assert(call->calls_node.next == LIST_POISON1);
	assert(call->calls_node.prev == LIST_POISON2);

	free(call);
}

struct q931_call *q931_find_call_by_reference(
	struct q931_interface *interface,
	enum q931_call_direction direction,
	q931_callref call_reference)
{
	struct q931_call *call;
	list_for_each_entry(call, &interface->calls, calls_node) {

		if (call->direction == direction &&
		    call->call_reference == call_reference) {
			return call;
		}
	}

	return NULL;
}

static void q931_call_stop_any_timer(struct q931_call *call)
{
	q931_call_stop_timer(call, T301);
	q931_call_stop_timer(call, T302);
	q931_call_stop_timer(call, T303);
	q931_call_stop_timer(call, T304);
	q931_call_stop_timer(call, T305);
	q931_call_stop_timer(call, T306);
	q931_call_stop_timer(call, T307);
	q931_call_stop_timer(call, T308);
	q931_call_stop_timer(call, T309);
	q931_call_stop_timer(call, T310);
	q931_call_stop_timer(call, T312);
	q931_call_stop_timer(call, T313);
	q931_call_stop_timer(call, T314);
	q931_call_stop_timer(call, T316);
	q931_call_stop_timer(call, T317);
	q931_call_stop_timer(call, T318);
	q931_call_stop_timer(call, T319);
	q931_call_stop_timer(call, T320);
	q931_call_stop_timer(call, T321);
	q931_call_stop_timer(call, T322);
}

#define q931_call_unexpected_primitive(call)	\
	_q931_call_unexpected_primitive((call), __FUNCTION__)

void _q931_call_unexpected_primitive(
	struct q931_call *call,
	const char *event)
{
	report_call(call, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_state_to_text(call->state));
}

#define q931_call_unexpected_timer(call)	\
	_q931_call_unexpected_timer((call), __FUNCTION__)

void _q931_call_unexpected_timer(
	struct q931_call *call,
	const char *event)
{
	report_call(call, LOG_ERR,
		"Unexpected %s in state %s\n",
		event,
		q931_state_to_text(call->state));
}

#define q931_call_unexpected_message(call)		\
	_q931_call_unexpected_unrecognized_message(	\
		(call), "Unexpected ", __FUNCTION__)

#define q931_call_unrecognized_message(call)		\
	_q931_call_unexpected_unrecognized_message(	\
		(call), "Unrecognized ", __FUNCTION__)

void _q931_call_unexpected_unrecognized_message(
	struct q931_call *call,
	const char *type,
	const char *event)
{
	if (call->state == N0_NULL_STATE) {
		q931_send_release_cause(call, call->dlc,
			Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	} if (call->state == U0_NULL_STATE) {
		q931_send_release_cause(call, call->dlc,
			Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE); //Checkme
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	} else {
		if (!q931_call_timer_running(call, T322)) {
			q931_send_status_enquiry(call, call->dlc);
			q931_call_start_timer(call, T322);
		}
	}

	report_call(call, LOG_ERR,
		"%s %s in state %s\n",
		type,
		event,
		q931_state_to_text(call->state));
}

static void q931_call_connect_channel(
	struct q931_channel *channel)
{
	assert(channel);

	channel->state = Q931_CHANSTATE_CONNECTED;

	if (channel->intf->connect_channel)
		channel->intf->connect_channel(channel);
}

static void q931_call_disconnect_channel(
	struct q931_channel *channel)
{
	assert(channel);

	channel->state = Q931_CHANSTATE_DISCONNECTED;

	if (channel->intf->disconnect_channel)
		channel->intf->disconnect_channel(channel);
}

static void q931_call_release_channel(
	struct q931_channel *channel)
{
	assert(channel);

	channel->state = Q931_CHANSTATE_AVAILABLE;
}

static void q931_call_start_tone(
	struct q931_channel *channel,
	enum q931_tone_type tone)
{
	assert(channel);

	if (channel->intf->start_tone)
		channel->intf->start_tone(channel, tone);
}

static void q931_call_stop_tone(
	struct q931_channel *channel)
{
	assert(channel);

	if (channel->intf->stop_tone)
		channel->intf->stop_tone(channel);
}

#define q931_call_primitive(call, primitive, ...)	\
	do {						\
		if ((call)->primitive)			\
			(call)->primitive(call);	\
	} while(0);

void q931_alerting_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_send_alerting(call, call->dlc);
		q931_call_set_state(call, N4_CALL_DELIVERED);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_send_alerting(call, call->dlc);
		q931_call_set_state(call, N4_CALL_DELIVERED);
	break;

	case U6_CALL_PRESENT:
		// B-channel selection
		q931_send_alerting(call, call->dlc);
		q931_call_set_state(call, U7_CALL_RECEIVED);

		// NOTE 2 : IN THE CASE OF A POINT-TO-POINT CONNECTION
		//          CONNECTION TO THE B-CHANNEL MAY BE MADE AT
		//          THIS POINT REF. 5.2.3.1d.

		q931_call_set_state(call, U7_CALL_RECEIVED);
	break;

	case U8_CONNECT_REQUEST:
		q931_send_alerting(call, call->dlc);
		q931_call_set_state(call, U7_CALL_RECEIVED);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_send_alerting(call, call->dlc);
		q931_call_set_state(call, U7_CALL_RECEIVED);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_disconnect_request(struct q931_call *call,
	enum q931_ie_cause_value cause)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);

		call->disconnect_cause = cause;

		if (call->tones_option) {
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_tone(call->channel, Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_call_disconnect_channel(call->channel);
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		call->disconnect_cause = cause;

		if (call->tones_option) {
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_tone(call->channel, Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_call_disconnect_channel(call->channel);
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N6_CALL_PRESENT:
		q931_call_stop_timer(call, T303);

		if (call->broadcast_setup) {
			q931_call_disconnect_channel(call->channel);
			q931_call_set_state(call, N22_CALL_ABORT);
		} else {
			call->disconnect_cause = cause;

			if (call->tones_option) {
				q931_send_disconnect_pi(call, call->dlc,
					call->disconnect_cause);
				q931_call_start_tone(call->channel, Q931_TONE_HANGUP);
				q931_call_start_timer(call, T306);
			} else {
				q931_call_disconnect_channel(call->channel);
				q931_send_disconnect(call, call->dlc,
					call->disconnect_cause);
				q931_call_start_timer(call, T305);
			}

			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N7_CALL_RECEIVED:
		q931_call_stop_timer(call, T301);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, cause);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		} else {
			call->disconnect_cause = cause;

			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, cause);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		} else {
			call->disconnect_cause = cause;
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, cause);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		} else {
			q931_send_disconnect(call, call->dlc, cause);
			q931_call_release_channel(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case N10_ACTIVE:
		call->disconnect_cause = cause;

		if (call->tones_option) {
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_tone(call->channel, Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_call_release_channel(call->channel);
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N15_SUSPEND_REQUEST:
		call->disconnect_cause = cause;

		if (call->tones_option) {
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_tone(call->channel, Q931_TONE_HANGUP);
			q931_call_start_timer(call, T306);
		} else {
			q931_call_disconnect_channel(call->channel);
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces, cause);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		} else {
			call->disconnect_cause = cause;

			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_disconnect_channel(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T308);
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = cause;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
	break;

	case U0_NULL_STATE:
	case U6_CALL_PRESENT:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_info_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
		q931_send_info(call, call->dlc); // ???!? FIXME
	break;

	case N6_CALL_PRESENT:
		// Save event until completion of a transition???
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_info_request(ces);
			}
		} else {
			q931_send_info(call, call->dlc);
		}
	break;

	case U1_CALL_INITIATED:
		// Do nothing
	break;

	case U2_OVERLAP_SENDING:
		q931_send_info(call, call->dlc);

		if (q931_timer_pending(&call->T304))
			q931_call_start_timer(call, T304);
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
		q931_send_info(call, call->dlc);
	break;

	case U0_NULL_STATE:
	case U6_CALL_PRESENT:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:

//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_more_info_request(struct q931_call *call)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_call_start_timer(call, T302);
		q931_send_setup_acknowledge_channel(call, call->dlc,
			call->channel);
		q931_call_connect_channel(call->channel);

		if (strlen(call->called_number)) {
		} else {
			if (call->tones_option)
				q931_call_start_tone(call->channel,
					Q931_TONE_DIAL);
		}

		q931_call_set_state(call, N2_OVERLAP_SENDING);
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_info_request(ces);
			}
		} else {
			q931_send_info(call, call->dlc);
			q931_call_start_timer(call, T304);
		}
	break;

	case U6_CALL_PRESENT:
		// B channel selection
		q931_send_setup_acknowledge(call, call->dlc);
		q931_call_start_timer(call, T302);
		q931_call_set_state(call, U25_OVERLAP_RECEIVING);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
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

//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
	
}

void q931_notify_request(struct q931_call *call)
{
	switch (call->state) {
	case N0_NULL_STATE:
		// Do nothing
	break;

	case N10_ACTIVE:
	case N15_SUSPEND_REQUEST:
		q931_send_notify(call, call->dlc);
	break;

	case U10_ACTIVE:
		q931_send_notify(call, call->dlc);
	break;

	case U0_NULL_STATE:
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
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_proceeding_request(struct q931_call *call)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_send_call_proceeding_channel(call, call->dlc,
			call->channel);
		q931_call_connect_channel(call->channel);
		q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);
	break;

	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_send_call_proceeding(call, call->dlc);
		q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);
	break;

	case U6_CALL_PRESENT:
		// B channel selection
		q931_send_call_proceeding(call, call->dlc);
		q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_send_call_proceeding(call, call->dlc);
		q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_progress_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
		q931_send_progress(call, call->dlc);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_send_progress(call, call->dlc);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_send_release_complete_cause(call, call->dlc, cause);
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
	break;


	case U6_CALL_PRESENT:
		// Cause == "Incompatible User?"
		q931_send_release_complete(call, call->dlc);
		q931_intf_del_call(call);
		q931_call_set_state(call, U0_NULL_STATE);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_release_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_send_release(call, call->dlc);
		q931_call_release_channel(call->channel);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		q931_send_release(call, call->dlc);
		q931_call_release_channel(call->channel);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N6_CALL_PRESENT:
		q931_call_stop_timer(call, T303);

		if (call->broadcast_setup) {
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N22_CALL_ABORT);
			q931_call_primitive(call, release_indication);
		} else {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NORMAL_CALL_CLEARING);
			}

			q931_call_stop_timer(call, T301);
			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312))
				q931_call_set_state(call, N22_CALL_ABORT);
			else
				q931_call_set_state(call, N0_NULL_STATE);

			q931_call_primitive(call, release_indication);
		} else {
			q931_send_release(call, call->dlc);
			q931_call_stop_timer(call, T301);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NORMAL_CALL_CLEARING);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312))
				q931_call_set_state(call, N22_CALL_ABORT);
			else
				q931_call_set_state(call, N0_NULL_STATE);

			q931_call_primitive(call, release_indication);
		} else {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NORMAL_CALL_CLEARING);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312))
				q931_call_set_state(call, N22_CALL_ABORT);
			else
				q931_call_set_state(call, N0_NULL_STATE);

			q931_call_primitive(call, release_indication);
		} else {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		}
	break;

	case N10_ACTIVE:
		q931_send_release(call, call->dlc);
		q931_call_release_channel(call->channel);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N11_DISCONNECT_REQUEST:
		q931_send_release_cause(call, call->dlc,
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N15_SUSPEND_REQUEST:
		q931_send_release(call, call->dlc);
		q931_call_release_channel(call->channel);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);

		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NORMAL_CALL_CLEARING);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312))
				q931_call_set_state(call, N22_CALL_ABORT);
			else
				q931_call_set_state(call, N0_NULL_STATE);

			q931_call_primitive(call, release_indication);
		} else {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
		}
	break;

	case U12_DISCONNECT_INDICATION:
		q931_call_disconnect_channel(call->channel);
		q931_send_release(call, call->dlc);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

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
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N12_DISCONNECT_INDICATION:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_request(struct q931_call *call)
{
	// BRI only

	switch (call->state) {
	case U0_NULL_STATE:
		q931_send_resume(call, call->dlc);
		q931_call_start_timer(call, T318);
		q931_call_set_state(call, U17_RESUME_REQUEST);
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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause)
{
	switch (call->state) {
	case N17_RESUME_REQUEST:
		q931_send_resume_reject(call, call->dlc, cause);
		q931_intf_del_call(call);
		q931_call_set_state(call, N0_NULL_STATE);
	break;

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
	case U25_OVERLAP_RECEIVING:
		q931_call_unexpected_message(call);
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
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_resume_response(struct q931_call *call)
{
	switch (call->state) {
	case N17_RESUME_REQUEST:
		q931_send_resume_acknowledge(call, call->dlc);
		q931_call_set_state(call, N10_ACTIVE);
	break;

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
	case U25_OVERLAP_RECEIVING:
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
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_setup_complete_request(struct q931_call *call)
{
	switch (call->state) {
	case N8_CONNECT_REQUEST:
		q931_call_connect_channel(call->channel);

		if (call->broadcast_setup) {
			call->selected_ces = call->preselected_ces;
			call->dlc = call->preselected_ces->dlc;
			q931_send_connect_acknowledge(call, call->dlc);
			q931_ces_free(call->selected_ces);

			struct q931_ces *ces = NULL;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
			}
		} else {
			q931_send_connect_acknowledge(call, call->dlc);
		}

		q931_call_set_state(call, N10_ACTIVE);
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_setup_request(struct q931_call *call)
{
	switch (call->state) {
	case N0_NULL_STATE:
		call->channel = q931_channel_alloc(call);
		if (call->channel) {
			q931_call_start_timer(call, T303);

			if (call->interface->type ==
			      Q931_INTF_TYPE_BRA_MULTIPOINT) {
				call->broadcast_setup = TRUE;
				q931_call_start_timer(call, T312);
				q931_send_setup_channel(call, call->dlc,
					Q931_SETUP_BROADCAST,
					call->channel);
			} else {
				q931_send_setup_channel(call,
					&call->interface->bc_dlc,
					Q931_SETUP_POINT_TO_POINT,
					call->channel);
			}

			q931_call_set_state(call, N6_CALL_PRESENT);
		} else {
			q931_call_primitive(call, release_indication);
		}
	break;

	case U0_NULL_STATE:
		// Set channel identity????
		q931_send_setup(call, call->dlc, Q931_SETUP_POINT_TO_POINT);
		q931_call_start_timer(call, T303);
		q931_call_set_state(call, U1_CALL_INITIATED);
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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_setup_response(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_send_connect(call, call->dlc);
		q931_call_set_state(call, N10_ACTIVE);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_send_connect(call, call->dlc);
		q931_call_set_state(call, N10_ACTIVE);
	break;

	case N4_CALL_DELIVERED:
		q931_send_connect(call, call->dlc);
		q931_call_set_state(call, N10_ACTIVE);
	break;

	case U6_CALL_PRESENT:
		// B channel selection
		q931_send_connect(call, call->dlc);
		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	break;

	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
		q931_send_connect(call, call->dlc);
		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_send_connect(call, call->dlc);
		q931_call_start_timer(call, T313);
		q931_call_set_state(call, U8_CONNECT_REQUEST);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U8_CONNECT_REQUEST:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_status_enquiry_request(struct q931_call *call)
{
}

void q931_suspend_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause)
{
	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_send_suspend_reject(call, call->dlc, cause);
		q931_call_set_state(call, N10_ACTIVE);
	break;

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
	case U25_OVERLAP_RECEIVING:
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
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_suspend_response(struct q931_call *call)
{
	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_send_suspend_acknowledge(call, call->dlc);

		// NOTE: Timer T307 is running in the call control block

		q931_intf_del_call(call);

		q931_call_set_state(call, N0_NULL_STATE);
	break;

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
	case U25_OVERLAP_RECEIVING:
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
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_suspend_request(struct q931_call *call)
{
	switch (call->state) {
	case U10_ACTIVE:
		q931_send_suspend(call, call->dlc);
		q931_call_start_timer(call, T319);
		q931_call_set_state(call, U15_SUSPEND_REQUEST);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_alerting_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);
		q931_call_start_timer(call, T301);
		q931_call_set_state(call, N7_CALL_RECEIVED);
		// NOTE 1
		q931_call_primitive(call, alerting_indication);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		q931_call_start_timer(call, T301);

		// NOTE 1. T301 IS NOT USED IF THE
		//         NETWORK IMPLEMENTS ANOTHER
		//         INTERNAL ALERTING SUPERVISION
		//         TIMING FUNCTION.

		q931_call_set_state(call, N7_CALL_RECEIVED);
		q931_call_primitive(call, alerting_indication);
	break;


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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_connect_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		call->preselected_ces = ces;
		q931_call_stop_timer(call, T301);
		q931_call_set_state(call, N8_CONNECT_REQUEST);
		q931_call_primitive(call, connect_indication);
	break;

	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);
		call->preselected_ces = ces;
		q931_call_set_state(call, N8_CONNECT_REQUEST);
		q931_call_primitive(call, connect_indication);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		call->preselected_ces = ces;
		q931_call_set_state(call, N8_CONNECT_REQUEST);
		q931_call_primitive(call, connect_indication);
	break;


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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}

}

void q931_int_call_proceeding_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T304);
		q931_call_start_timer(call, T310);
		q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
		q931_call_primitive(call, proceeding_indication);
	break;


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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_release_complete_indication(
	struct q931_call *call,
	struct q931_ces *ces)
{
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
			// Network disconnect indication?
			if (0) {
				q931_call_primitive(call, release_indication);
			} else {
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}
		}
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_info_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N19_RELEASE_REQUEST:
	case N25_OVERLAP_RECEIVING:
		q931_call_primitive(call, info_indication);
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_progress_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		q931_call_primitive(call, progress_indication);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_call_stop_timer(call, T310);

		q931_call_primitive(call, progress_indication);
	break;

	case N25_OVERLAP_RECEIVING:
		q931_call_primitive(call, progress_indication);
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

void q931_int_release_indication(
	struct q931_call *call,
	struct q931_ces *ces)
{
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
			// Save cause
		} else {
			if (q931_timer_pending(&call->T312)) {
				// Save cause
			} else {
				q931_call_stop_timer(call, T310);
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		}
	}
	break;

	case N8_CONNECT_REQUEST:
		if (call->preselected_ces) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}

			q931_call_primitive(call, release_indication);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING: {
		int able_to_proceed = 0;

		// Are other CES able to proceed?
		// FIXME Is this correct? Specs are unclear! FIXME

		struct q931_ces *tces = NULL;
		list_for_each_entry(ces, &call->ces, node) {
			if (tces != ces &&
			    tces->state != N0_NULL_STATE &&
			    tces->state != N19_RELEASE_REQUEST) {

				able_to_proceed = 1;

				break;
			}
		}

		if (able_to_proceed) {
			// Save cause
		} else {
			// Is T312 running (THIS MAY BE WRONG)
			if (q931_timer_pending(&call->T312)) {
				// Save cause
			} else {
				q931_call_stop_timer(call, T310);

				q931_call_release_channel(call->channel);

				q931_intf_del_call(call);

				q931_call_set_state(call, N0_NULL_STATE);

				q931_call_primitive(call, release_indication); //With cause
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
			// Save cause
		} else {
			if (q931_timer_pending(&call->T312)) {
				// Save cause
			} else {
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
			}
		}
	}
	break;


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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

static void q931_timer_T301(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T301 fired\n");

//	q931_call_primitive(call, timeout_indication);

	switch (call->state) {
	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}
		} else {
			call->disconnect_cause =
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}

		q931_call_primitive(call, release_indication);
	break;

	default:
		q931_call_unexpected_primitive(call);
	break;
	}
}

static void q931_timer_T302(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T302 fired\n");

	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Is call info incomplete?
		if (1) {
			if (call->tones_option) {
				call->disconnect_cause =
					Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
				q931_send_disconnect_pi(call, call->dlc,
					call->disconnect_cause);
				q931_call_start_tone(call->channel,
					Q931_TONE_HANGUP);
				q931_call_start_timer(call, T306);
			} else {
				q931_call_disconnect_channel(call->channel);

				call->disconnect_cause =
					Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
				q931_send_disconnect(call, call->dlc,
					call->disconnect_cause);
				q931_call_start_timer(call, T305);
			}

			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		} else {
			q931_send_call_proceeding(call, call->dlc);
			q931_call_set_state(call, N3_OUTGOING_CALL_PROCEEDING);
			q931_call_primitive(call, timeout_indication);
		}
	break;

	case U25_OVERLAP_RECEIVING:
		// Call info is definitely incomplete?
		if (1) {
			q931_call_disconnect_channel(call->channel);
			q931_send_disconnect(call, call->dlc,
				Q931_IE_C_CV_INVALID_NUMBER_FORMAT);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		} else {
			// Which message???
			// Connect => q931_call_start_timer(call, T313);
			//		q931_call_set_state(call, U8_CONNECT_REQUEST);
			// Alerting => q931_call_set_state(call, U7_CALL_RECEIVED);
			// Callproc => q931_call_set_state(call, U9_INCOMING_CALL_PROCEEDING);
		}
	break;

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
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T303(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T303 fired\n");

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (!call->T303_fired) {
			if (call->broadcast_setup) {
				// REL COMP RCVD?
				if (1) {
					q931_call_release_channel(call->channel);
					q931_call_set_state(call, N22_CALL_ABORT);
					q931_call_primitive(call, release_indication);
				} else {
					q931_send_setup_channel(call, NULL,
						Q931_SETUP_BROADCAST,
						call->channel);
					q931_call_start_timer(call, T303);
					q931_call_start_timer(call, T312);
				}
			} else {
				q931_send_setup_channel(call, call->dlc,
					Q931_SETUP_POINT_TO_POINT,
					call->channel);
				q931_call_start_timer(call, T303);
			}
		} else {
			if (call->broadcast_setup) {
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N22_CALL_ABORT);
				q931_call_primitive(call, release_indication);
			} else {
				q931_call_start_timer(call, T305);
				call->disconnect_cause = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
				q931_send_disconnect(call, call->dlc,
					call->disconnect_cause);
				q931_call_set_state(call, N12_DISCONNECT_INDICATION);
				q931_call_primitive(call, disconnect_indication);
			}
		}
	break;

	case U1_CALL_INITIATED:
		if (call->T303_fired) {
			q931_send_setup(call, call->dlc,
				Q931_SETUP_POINT_TO_POINT);
			q931_call_start_timer(call, T303);
		} else {
			q931_call_set_state(call, U0_NULL_STATE);
			q931_intf_del_call(call);
		}
	break;

	case U0_NULL_STATE:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}

	call->T303_fired = TRUE;

}

static void q931_timer_T304(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T304 fired\n");

	switch (call->state) {
	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}
		} else {
			call->disconnect_cause =
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;

			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_disconnect_channel(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}

		q931_call_primitive(call, release_indication);
	break;

	case U2_OVERLAP_SENDING:
		// Disconnect B-channel if necessary

		q931_send_disconnect(call, call->dlc,
			Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);

		q931_call_primitive(call, setup_confirm);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
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
	case U25_OVERLAP_RECEIVING:
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
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}


static void q931_timer_T305(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T305 fired\n");

	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		q931_send_release_cause(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_send_release_cause(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

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
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T306(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T306 fired\n");

	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		q931_call_stop_tone(call->channel);
		q931_send_release_cause(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

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
	case U25_OVERLAP_RECEIVING:
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
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T307(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T307 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T307\n");
}

static void q931_timer_T308(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T308 fired\n");

	switch (call->state) {
	case N19_RELEASE_REQUEST:
		if (call->T308_fired) {
			q931_send_release(call, call->dlc);
// FIXME				call->release_cause);
			q931_call_start_timer(call, T308);
		} else {
			if (call->interface->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
				q931_call_release_channel(call->channel);
			} else {
				// Place B channel in mainenance condition
			}

			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_confirm);
		}
	break;

	case U19_RELEASE_REQUEST:
		// First timeout?
		if (1) {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
		} else {
			// Optional: Place B channel in maintainance condition

			// NOTE 2: THE OPTION OF PLACING THE B-CHANNEL IN THE MAINTENANCE CONDITION
			//         IS NOT APPLICABLE IN THE CASE OF POINT-TO-MULTIPOINT CONFIGURATIONS
			//        (REF SECTION 5.3.4.3)


			q931_call_release_channel(call->channel);

			q931_call_set_state(call, U0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_confirm); //ERROR
		}
	break;

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
	case U25_OVERLAP_RECEIVING:
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
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}

	call->T308_fired = TRUE;
}

static void q931_timer_T309(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T309 fired\n");

	switch (call->state) {
	case N10_ACTIVE:
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U10_ACTIVE:
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U0_NULL_STATE:
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
	case N0_NULL_STATE:
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
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T310(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T310 fired\n");

	switch (call->state) {
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
//	FIXME				call->release_cause);
			}

			q931_call_release_channel(call->channel);

			if (q931_timer_pending(&call->T312)) {
				q931_call_set_state(call, N22_CALL_ABORT);
			} else {
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}
		} else {
			call->disconnect_cause = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);

			q931_call_release_channel(call->channel);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		}

		q931_call_primitive(call, release_indication);
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T312(void *data)
{
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
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
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
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
				q931_call_primitive(call, release_indication);
		}
	}

	case N22_CALL_ABORT:
		// Clear T312 flag

		if (list_empty(&call->ces)) {
			// Network disconnect indication?
			if (0) {
				q931_call_primitive(call, release_indication);
			} else {
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_intf_del_call(call);
			}
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
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	}

	break;

	case N8_CONNECT_REQUEST:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N19_RELEASE_REQUEST:
		// Do nothing
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N17_RESUME_REQUEST:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T313(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T313 fired\n");

	switch (call->state) {
	case U8_CONNECT_REQUEST:
		call->disconnect_cause = Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive(call, setup_complete_indication);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}
}

static void q931_timer_T314(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T314 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T314\n");
}

static void q931_timer_T316(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T316 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T314\n");
}

static void q931_timer_T317(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T317 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T314\n");
}

static void q931_timer_T318(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T318 fired\n");

	switch (call->state) {
	case U17_RESUME_REQUEST:
		q931_send_release(call, call->dlc); // Cause #102 "RECOVERY ON TIMER EXPIRY"
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_timer(call);
	break;
	}

}

static void q931_timer_T319(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T319 fired\n");

	q931_call_primitive(call, suspend_confirm); // TIMEOUT ERROR
	q931_call_set_state(call, U10_ACTIVE);
}

static void q931_timer_T320(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T320 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T320\n");
}

static void q931_timer_T321(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T321 fired\n");

	report_call(call, LOG_ERR,
		"Unexpected timer T321\n");
}

static void q931_timer_T322(void *data)
{
	struct q931_call *call = data;

	report_call(call, LOG_DEBUG, "T322 fired\n");

	if (call->state == N0_NULL_STATE ||
	    call->state == U0_NULL_STATE) {
		q931_call_unexpected_timer(call);
		return;
	}

	// Status received
	if (0) {
	} else {
		// Max retrans exceeded
		if (0) {
			q931_send_release(call, call->dlc); // Cause #41
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
			q931_call_primitive(call, release_indication); // ERROR
		} else {
			q931_send_status_enquiry(call, call->dlc);
			q931_call_start_timer(call, T322);
		}
	}
}

inline static void q931_handle_alerting(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
// Ok, we received a response to SETUP, we should interpret IEs, in particular
// we must interpret channel_identification, and assing call->channel accordingly

	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T303);
				q931_ces_alerting_request(ces);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, alerting_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T303);

			if (call->channel) {
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, alerting_indication);
			} else {
				q931_send_release_cause(call, call->dlc,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, release_indication);
			}
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_alerting_request(ces);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			q931_ces_release_request(ces,
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T310);
				q931_ces_alerting_request(ces);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, alerting_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_start_timer(call, T301);
			q931_call_set_state(call, N7_CALL_RECEIVED);
			q931_call_primitive(call, alerting_indication);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, dlc);

		// FIXME
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//         IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312))
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
		else
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_alerting_request(ces);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N7_CALL_RECEIVED);
				q931_call_primitive(call, alerting_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_start_timer(call, T301);
			q931_call_set_state(call, N7_CALL_RECEIVED);
			q931_call_primitive(call, alerting_indication);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		// B-Channel control? (ref 5.1.2, 5.1.6, 5.1.7)
		q931_call_start_timer(call, T301);
		q931_call_set_state(call, U4_CALL_DELIVERED);
		// NOTE 3,2,1
		q931_call_primitive(call, alerting_indication);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		// B-Channel control
		// NOTE 3
		q931_call_set_state(call, U4_CALL_DELIVERED);
		q931_call_primitive(call, alerting_indication);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
		// B-Channel control
		q931_call_set_state(call, U4_CALL_DELIVERED);
		q931_call_primitive(call, alerting_indication);
	break;

	case U0_NULL_STATE:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_call_proceeding(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T303);
				q931_ces_call_proceeding_request(ces);
				q931_call_start_timer(call, T310);
				q931_call_primitive(call, proceeding_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T303);

			if (call->channel) {
				q931_call_start_timer(call, T310);
				q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
				q931_call_primitive(call, proceeding_indication);
			} else {
				q931_send_release_cause(call, call->dlc,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, release_indication);
			}
		}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_call_proceeding_request(ces);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			q931_ces_release_request(ces,
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, dlc);

		// FIXME
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//         IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312))
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
		else
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_call_proceeding_request(ces);
				q931_call_start_timer(call, T310);
				q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
				q931_call_primitive(call, proceeding_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_start_timer(call, T310);
			q931_call_set_state(call, N9_INCOMING_CALL_PROCEEDING);
			q931_call_primitive(call, proceeding_indication);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);

		// NOTE 1: T304 IS OPTIONAL
		// NOTE 2: THE USER MAY CLEAR THE CALL AT THIS POINT IF THE CHANNEL IDENTITY
		//         RETURNED BY THE NETWORK IS UNACCEPTABLE.
		//         ONLY APPLICABLE FOR THE PROCEDURES DEFINED IN ANNEX D.
		// NOTE 3:
		//         THE SETUP ACKNOWLEDGE MESSAGE WILL NOT BE RECEIVED IF THE SENDING


		// B-Channel Control? (REF 5.1.2)
		q931_call_set_state(call, U3_OUTGOING_CALL_PROCEEDING);
		q931_call_primitive(call, proceeding_indication);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		// B-Channel control
		// NOTE 3
		q931_call_set_state(call, U3_OUTGOING_CALL_PROCEEDING);
		q931_call_primitive(call, proceeding_indication);
	break;

	case U0_NULL_STATE:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_connect(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T303);
				call->preselected_ces = ces;
				q931_ces_connect_request(ces);
				q931_call_start_timer(call, T301);
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, setup_confirm);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T303);

			if (call->channel) {
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, setup_confirm);
			} else {
				q931_send_release_cause(call, call->dlc,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, release_indication);
			}
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T301);
				call->preselected_ces = ces;
				q931_ces_connect_request(ces);
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, connect_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T301);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, connect_indication);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			q931_ces_release_request(ces,
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_call_stop_timer(call, T310);
				call->preselected_ces = ces;
				q931_ces_connect_request(ces);
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, connect_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, connect_indication);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, dlc);

		// FIXME
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//         IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312))
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
		else
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				call->preselected_ces = ces;
				q931_ces_connect_request(ces);
				q931_call_set_state(call, N8_CONNECT_REQUEST);
				q931_call_primitive(call, connect_indication);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_set_state(call, N8_CONNECT_REQUEST);
			q931_call_primitive(call, connect_indication);
		}
	break;


	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		// B-channel control (if not yet expected)
		q931_send_connect_acknowledge(call, call->dlc);
		q931_call_set_state(call, U10_ACTIVE);

		q931_call_primitive(call, setup_confirm);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_call_connect_channel(call->channel);
		q931_send_connect_acknowledge(call, call->dlc);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, setup_confirm);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
		q931_call_connect_channel(call->channel);
		q931_send_connect_acknowledge(call, call->dlc);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, setup_confirm);
	break;

	case U4_CALL_DELIVERED:
		q931_call_connect_channel(call->channel);
		q931_send_connect_acknowledge(call, call->dlc);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, setup_confirm);
	break;

	case U0_NULL_STATE:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_connect_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N10_ACTIVE:
		// Do nothing
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T313);
		q931_call_connect_channel(call->channel);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, setup_complete_indication);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U12_DISCONNECT_INDICATION:
	case U15_SUSPEND_REQUEST:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
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
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_progress(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		} else {
			q931_call_primitive(call, progress_indication);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_primitive(call, progress_indication);
		}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		} else {
			q931_call_primitive(call, progress_indication);
		}
	break;


	case U2_OVERLAP_SENDING:
		// B-channel control

		// If interworking
		if (0)
			q931_call_stop_timer(call, T304);

		q931_call_primitive(call, progress_indication);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
		// B-channel control
		q931_call_primitive(call, progress_indication);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N8_CONNECT_REQUEST:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_setup(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N0_NULL_STATE:
		call->channel = q931_channel_alloc(call);
		if (!call->channel) {
			q931_send_release_complete_cause(
				call, call->dlc,
				Q931_IE_C_CV_NO_CIRCUIT_CANNEL_AVAILABLE);
			q931_intf_del_call(call);

			return;
		}

		q931_call_set_state(call, N1_CALL_INITIATED);

		{
		int i;

		for(i=0; i<ies_cnt; i++) {
			if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
				call->sending_complete = TRUE;
			} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
				if (ies[i].size < 2) {
					report_dlc(dlc, LOG_ERR, "IE size < 2\n");
					// Send status with cause code
					return;
				}

				if (ies[i].size - 1 > Q931_MAX_DIGITS) {
					report_dlc(dlc, LOG_ERR, "IE size > Q931_MAX_DIGITS + 1\n");
					// Send status with cause code
					return;
				}

				char *number = ies[i].data + 1;

				if (number[ies[i].size - 1] == '#') {
					call->sending_complete = TRUE;
					strncat(call->called_number, number,
						ies[i].size - 1);
				} else {
					strncat(call->called_number, number,
						ies[i].size - 2);
				}
			}
		}
		}

		q931_call_primitive(call, interface->setup_indication);
	break;

	case U0_NULL_STATE:
		call->channel = q931_channel_select(call, ies, ies_cnt);
		if (call->channel) {
			q931_call_set_state(call, U6_CALL_PRESENT);
			q931_call_primitive(call, interface->setup_indication);
		} else {
			q931_send_release_complete(call, call->dlc,
				Q931_IE_C_CV_NO_CIRCUIT_CANNEL_AVAILABLE);
			q931_call_set_state(call, U0_NULL_STATE);
			q931_intf_del_call(call);
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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
	case N22_CALL_ABORT:
	case N25_OVERLAP_RECEIVING:
		// Do nothing
	break;
	}
}

inline static void q931_handle_setup_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcast_setup) {
			if (call->channel) {
				q931_call_stop_timer(call, T303);

				struct q931_ces *ces;
				ces = q931_ces_alloc(call, dlc);

				q931_ces_setup_ack_request(ces);
				q931_call_start_timer(call, T304);
				q931_call_set_state(call, N25_OVERLAP_RECEIVING);
				q931_call_primitive(call, more_info_indication);
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call, dlc);

				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_call_stop_timer(call, T303);

			if (call->channel) {
				q931_call_start_timer(call, T304);
				q931_call_set_state(call, N25_OVERLAP_RECEIVING);
				q931_call_primitive(call, more_info_indication);
			} else {
				q931_send_release_cause(call, call->dlc,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
				q931_call_start_timer(call, T308);
				q931_call_set_state(call, N19_RELEASE_REQUEST);
				q931_call_primitive(call, release_indication);
			}
		}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_setup_ack_request(ces);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			q931_ces_release_request(ces,
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case N22_CALL_ABORT: {
		struct q931_ces *ces;
		ces = q931_ces_alloc(call, dlc);

		// FIXME
		// NOTE 1. THE CAUSE SENT DEPENDS ON WHETHER T312 IS STILL
		//	   RUNNING OR NOT, WHICH IS INDICATED BY T312 FLAG.
		//         IF T312 IS STILL RUNNING, THE CAUSE SENT ALSO
		//	   DEPENDS ON WHETHER A NETWORK DISCONNECT INDICATION
		//	   HAS BEEN RECEIVED OR NOT.

		if (q931_call_timer_running(call, T312))
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
		else
			q931_ces_release_request(ces,
				Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY);
	}
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call, dlc);

			if (call->channel) {
				q931_ces_setup_ack_request(ces);
			} else {
				q931_ces_release_request(ces,
					Q931_IE_C_CV_CHANNEL_UNACCEPTABLE);
			}
		} else {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		// B-channel control
		q931_call_start_timer(call, T304);
		q931_call_set_state(call, U2_OVERLAP_SENDING);
		q931_call_primitive(call, more_info_indication);
	break;

	case U0_NULL_STATE:
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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N19_RELEASE_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_disconnect(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N4_CALL_DELIVERED:
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);

			//IN THE CASE OF A BROADCAST SETUP,
			//THE CALL STATE RETURNED IN THE STATUS
			//MESSAGE SHOULD BE STATE 6.
		} else {
			q931_call_stop_timer(call, T301);
			q931_call_disconnect_channel(call->channel);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call, disconnect_indication);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);

			//IN THE CASE OF A BROADCAST SETUP,
			//THE CALL STATE RETURNED IN THE STATUS
			//MESSAGE SHOULD BE STATE 6.
		} else {
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call, disconnect_indication);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
			// NOTE 2
		} else {
			q931_call_stop_timer(call, T310);
			q931_call_disconnect_channel(call->channel);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call, disconnect_indication);
		}
	break;

	case N10_ACTIVE:
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N12_DISCONNECT_INDICATION:
		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_call_stop_tone(call->channel);
		q931_send_release(call, call->dlc);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, N19_RELEASE_REQUEST);
	break;

	case N17_RESUME_REQUEST:
		q931_call_disconnect_channel(call->channel);
		q931_call_set_state(call, N11_DISCONNECT_REQUEST);
		q931_call_primitive(call, disconnect_indication);
	break;

	case N19_RELEASE_REQUEST:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
			// NOTE 1. IN THE CASE OF A BROADCAST SETUP, THE CALL STATE
			//        RETURNED IN THE STATUS MESSAGE SHOULD BE STATE 6.
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_disconnect_channel(call->channel);
			q931_call_set_state(call, N11_DISCONNECT_REQUEST);
			q931_call_primitive(call, disconnect_indication);
		}
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		// B-channel control
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
		// B-channel control
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T313);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_call_stop_timer(call, T305);
		q931_send_release(call, call->dlc);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

	case U15_SUSPEND_REQUEST:
		q931_call_stop_timer(call, T319);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		q931_send_release(call, call->dlc);
		q931_call_start_timer(call, T308);
		q931_call_set_state(call, U19_RELEASE_REQUEST);
	break;

	case U19_RELEASE_REQUEST:
		// Do nothing
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_set_state(call, U12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U12_DISCONNECT_INDICATION:
	case N0_NULL_STATE:
	case N6_CALL_PRESENT:
	case N11_DISCONNECT_REQUEST:
	case N15_SUSPEND_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_message(call);
	break;
	}


/*
	int inband_info = FALSE;

	{
	int i;
	int ie_count = 0;

	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_PROGRESS_INDICATOR) {
			struct q931_ie_progress_indicator_onwire_3_4 *ie =
				(struct q931_ie_progress_indicator_onwire_3_4 *)(ies[i].data);

			if (ie->coding_standard == Q931_IE_PI_CS_CCITT &&
			    ie->progress_description ==
				Q931_IE_PI_PD_IN_BAND_INFORMATION_OR_APPROPRIATE_PATTERN_AVAILABLE) {
				inband_info = TRUE;
				ie_count++;

				if (ie_count == 2)
					break;
			}
		}
	}
	}

	if (inband_info) {
		q931_call_connect_channel(call->channel);

		q931_call_primitive(call, interface->role == LAPD_ROLE_TE);
			call->user_state = U12_DISCONNECT_INDICATION;
		else
			call->net_state = N12_DISCONNECT_INDICATION;
	} else {
		q931_call_disconnect_channel(call->channel);

		q931_call_start_timer(call, T308);
		q931_call_primitive(call, interface->role == LAPD_ROLE_TE);
			call->user_state = U19_RELEASE_REQUEST;
		else
			call->net_state = N19_RELEASE_REQUEST;

		q931_send_release(call, call->dlc);
	}

*/
}

inline static void q931_handle_release(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{

	switch (call->state) {
	case N0_NULL_STATE:
	case U0_NULL_STATE:
		q931_send_release_complete_cause(
			call, call->dlc,
			Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);
		q931_intf_del_call(call);
	break; 

	case N1_CALL_INITIATED:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N6_CALL_PRESENT:
		q931_send_release_complete(call, call->dlc);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T303);
			q931_call_release_channel(call->channel);
			q931_intf_del_call(call);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N7_CALL_RECEIVED:
		q931_send_release_complete(call, call->dlc);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T302);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N8_CONNECT_REQUEST:
		q931_send_release_complete(call, call->dlc);

		if (!call->broadcast_setup) {
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		q931_send_release_complete(call, call->dlc);

		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T310);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N12_DISCONNECT_INDICATION:
		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_call_stop_tone(call->channel);
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N19_RELEASE_REQUEST:
		q931_call_stop_timer(call, T308);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_confirm);
	break;

	case N22_CALL_ABORT:
		q931_send_release_complete(call, call->dlc);
	break;

	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_send_release_complete(call, call->dlc);
		} else {
			q931_call_stop_timer(call, T304);
			q931_call_release_channel(call->channel);
			q931_send_release_complete(call, call->dlc);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		q931_send_release_complete(call, call->dlc);
		// Is this state correct or are the specs right?
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_send_release_complete(call, call->dlc);
		// Is this state correct or are the specs right?
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U12_DISCONNECT_INDICATION:
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U6_CALL_PRESENT:
		q931_intf_del_call(call);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_primitive(call, release_indication);
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T313);
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_call_stop_timer(call, T305);
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U15_SUSPEND_REQUEST:
		q931_call_stop_timer(call, T319);
		q931_call_release_channel(call->channel);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		q931_send_release_complete(call, call->dlc);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U19_RELEASE_REQUEST:
		q931_call_stop_timer(call, T308);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_confirm);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_send_release_complete(call, call->dlc);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}

}

inline static void q931_handle_release_complete(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N0_NULL_STATE:
		// Do nothing
	break;

	case N1_CALL_INITIATED:
		q931_call_release_channel(call->channel);
		q931_intf_del_call(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call, release_indication);
	break;

	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_release_channel(call->channel);
		q931_intf_del_call(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call, release_indication);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		q931_call_release_channel(call->channel);
		q931_intf_del_call(call);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_call_primitive(call, release_indication);
	break;

	case N6_CALL_PRESENT:

		if (call->broadcast_setup) {
			// Save cause
		} else {
			q931_call_stop_timer(call, T303);
			q931_call_release_channel(call->channel);
			q931_intf_del_call(call);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N7_CALL_RECEIVED:
		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T301);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (!call->broadcast_setup) {
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T310);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N12_DISCONNECT_INDICATION:
		q931_call_stop_timer(call, T305);
		q931_call_stop_timer(call, T306);
		q931_call_stop_tone(call->channel);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case N19_RELEASE_REQUEST:
		q931_call_stop_timer(call, T308);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, N0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_confirm);
	break;

	case N22_CALL_ABORT:
		// Do nothing
	break;

	case N25_OVERLAP_RECEIVING:
		if (!call->broadcast_setup) {
			q931_call_stop_timer(call, T304);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;

	case U0_NULL_STATE:
		// Do nothing
	break;

	case U1_CALL_INITIATED:
		q931_call_stop_timer(call, T303);
		q931_intf_del_call(call);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_primitive(call, reject_indication);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_intf_del_call(call);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_primitive(call, reject_indication);
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U9_INCOMING_CALL_PROCEEDING:
	case U10_ACTIVE:
	case U12_DISCONNECT_INDICATION:
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U8_CONNECT_REQUEST:
		q931_call_stop_timer(call, T313);
		q931_call_release_channel(call->channel);
		q931_intf_del_call(call);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_call_primitive(call, release_indication);
	break;

	case U11_DISCONNECT_REQUEST:
		q931_call_stop_timer(call, T305);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U15_SUSPEND_REQUEST:
		q931_call_stop_timer(call, T319);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication);
	break;

	case U19_RELEASE_REQUEST:
		q931_call_stop_timer(call, T308);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_confirm);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_confirm);
	break;

	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_restart(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_restart_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_status(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// FIXME complete implementation pg. 95

	switch (call->state) {
	case N0_NULL_STATE:
		// CS!=0?
		if (1) {
			q931_send_release_cause(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
			q931_call_set_state(call, N19_RELEASE_REQUEST);
			q931_call_stop_timer(call, T308);
		}
	break;

	case N19_RELEASE_REQUEST:
		// CS=0?
		if (1) {
			q931_call_stop_timer(call, T308);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, status_indication); // WITH ERROR
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
	case N25_OVERLAP_RECEIVING:
/*
		// Status CS=M ?
		if (1) {
			if (q931_timer_pending(&call->T322)) {
				// Cause = #97 ?
				if (1) {
					// Save cause and call state (!?!?!?)
				} else if(1) { // Cause == #30 ?
					q931_call_stop_timer(call, T322);
				}
			}

			// M=0 ?
			if (1) {
				q931_intf_del_call(call);
				q931_call_set_state(call, N0_NULL_STATE);
				q931_call_primitive(call, status_indication); // WITH ERROR
			} else {
				// Compatible state?
				if (1) {
					// NOTE 1. FURTHER ACTIONS ARE AN IMPLEMENTATION OPTION.
				} else {
					q931_send_release(call, call->dlc);
					q931_call_start_timer(call, T308);
					q931_call_set_state(call, N19_RELEASE_REQUEST);
					q931_call_primitive(call, status_indication); // WITH ERROR
				}
			}
		}
*/
	break;

	case U0_NULL_STATE:
		// IF CS<>0
		if (1) {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
			q931_call_set_state(call, U19_RELEASE_REQUEST);
		}
	break;

	case U19_RELEASE_REQUEST:
		// Status (CS) ?
		if (1) {
			// Cause value #30 ?
			if (0) {
				q931_call_stop_timer(call, T322);
			}

			// CS=0?
			if (0) {
				q931_call_stop_timer(call, T308);
				q931_call_primitive(call, status_indication); //ERROR
				q931_call_release_channel(call->channel);
				q931_call_set_state(call, U0_NULL_STATE);
				q931_intf_del_call(call);
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
	case U25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_status_enquiry(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	if (call->state == N0_NULL_STATE ||
	    call->state == U0_NULL_STATE) {
		q931_call_unexpected_message(call);
		return;
	}

	q931_send_status(call, call->dlc,
		Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY);
}

inline static void q931_handle_user_information(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	
}

inline static void q931_handle_segment(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_congestion_control(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_info(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T302);
		q931_call_stop_tone(call->channel);

		int sending_complete = FALSE;
		int i;

		for(i=0; i<ies_cnt; i++) {
			if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
				sending_complete = TRUE;
			} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
				if (ies[i].size < 2) {
					report_call(call, LOG_ERR,
						"IE size < 2\n");
					// Send status with cause code
					return;
				}

				if (strlen(call->called_number) + ies[i].size - 1
				    > Q931_MAX_DIGITS) {
					report_call(call, LOG_ERR,
						"Called number overflow\n");
					// Send status with cause code
					return;
				}

				char *number = ies[i].data + 1;

				if (number[ies[i].size - 1] == '#') {
					sending_complete = TRUE;
					strncat(call->called_number, number,
						ies[i].size - 2);
				} else {
					report_call(call, LOG_INFO,
						"strncat(%s,%c,%d)\n",call->called_number,
						*number,ies[i].size - 1);

					strncat(call->called_number, number,
						ies[i].size - 1);
				}
			}
		}

		q931_call_start_timer(call, T302);
		q931_call_primitive(call, info_indication);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N19_RELEASE_REQUEST:
		q931_call_primitive(call, info_indication);
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N25_OVERLAP_RECEIVING:
		if (call->broadcast_setup) {
			q931_send_status(call, call->dlc,
				Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE);
		} else {
			q931_call_primitive(call, info_indication);
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
		q931_call_primitive(call, info_indication);
	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_start_timer(call, T302);
		q931_call_primitive(call, info_indication);
	break;

	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U7_CALL_RECEIVED:
	case U17_RESUME_REQUEST:
	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N6_CALL_PRESENT:
	case N15_SUSPEND_REQUEST:
	case N17_RESUME_REQUEST:
	case N22_CALL_ABORT:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_facility(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_notify(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N10_ACTIVE:
	case U10_ACTIVE:
	case U11_DISCONNECT_REQUEST:
	case U15_SUSPEND_REQUEST:
		q931_call_primitive(call, notify_indication);
	break;


	case U0_NULL_STATE:
	case U1_CALL_INITIATED:
	case U2_OVERLAP_SENDING:
	case U3_OUTGOING_CALL_PROCEEDING:
	case U4_CALL_DELIVERED:
	case U6_CALL_PRESENT:
	case U7_CALL_RECEIVED:
	case U8_CONNECT_REQUEST:
	case U9_INCOMING_CALL_PROCEEDING:
	case U12_DISCONNECT_INDICATION:
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
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
		// Telecom Italia sends NOTIFY
	break;

//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_hold(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_hold_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_hold_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_resume(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only on BRIs

	switch (call->state) {
	case N0_NULL_STATE:
		if (call->interface->type == Q931_INTF_TYPE_PRA) {
			q931_call_unexpected_message(call);
			return;
		}

		q931_call_set_state(call, N17_RESUME_REQUEST);
		q931_call_primitive(call, resume_indication);
	break;


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
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_resume_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only on BRIs

	switch (call->state) {
	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		// Connect to B channel
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, resume_confirm);
	break;

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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_resume_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only on BRIs

	switch (call->state) {
	case U17_RESUME_REQUEST:
		q931_call_stop_timer(call, T318);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, resume_confirm); // TIMEOUT_ERROR
	break;

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
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_suspend(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N10_ACTIVE:
		if (call->interface->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
		    call->interface->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
			q931_call_set_state(call, N15_SUSPEND_REQUEST);
			q931_call_primitive(call, suspend_indication);
		} else {
			q931_send_suspend_reject(call, call->dlc,
				Q931_IE_C_CV_FACILITY_REJECTED);
		}
	break;

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
	case U25_OVERLAP_RECEIVING:
	case N0_NULL_STATE:
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
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_suspend_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only for BRI

	switch (call->state) {
	case U15_SUSPEND_REQUEST:
		q931_call_stop_timer(call, T319);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, suspend_confirm);
	break;

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
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

inline static void q931_handle_suspend_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only for BRI

	switch (call->state) {
	case U15_SUSPEND_REQUEST:
		q931_call_stop_timer(call, T319);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U10_ACTIVE);
		q931_call_primitive(call, suspend_confirm); // WITH ERROR
	break;

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
	case U17_RESUME_REQUEST:
	case U19_RELEASE_REQUEST:
	case U25_OVERLAP_RECEIVING:
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
	case N25_OVERLAP_RECEIVING:
//	default:
		q931_call_unexpected_message(call);
	break;
	}
}

void q931_call_dl_establish_indication(struct q931_call *call)
{
	if (call->state == N2_OVERLAP_SENDING) {
		q931_call_stop_any_timer(call);

		if (call->tones_option) {
			q931_call_disconnect_channel(call->channel);
			call->disconnect_cause = Q931_IE_C_CV_TEMPORARY_FAILURE;
			q931_send_disconnect(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
		} else {
			call->disconnect_cause = Q931_IE_C_CV_TEMPORARY_FAILURE;
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_tone(call->channel, Q931_TONE_FAILURE);
			q931_call_start_timer(call, T306);
		}

		q931_call_set_state(call, N12_DISCONNECT_INDICATION);
		q931_call_primitive(call, disconnect_indication);
	} else if(call->state == N25_OVERLAP_RECEIVING) {
		if (!call->broadcast_setup) {
			call->disconnect_cause = Q931_IE_C_CV_TEMPORARY_FAILURE;
			q931_send_disconnect_pi(call, call->dlc,
				call->disconnect_cause);
			q931_call_start_timer(call, T305);
			q931_call_set_state(call, N12_DISCONNECT_INDICATION);
			q931_call_primitive(call, release_indication);
		}
	} else if (call->state == U2_OVERLAP_SENDING) {
		q931_call_stop_timer(call, T304);
		q931_call_disconnect_channel(call->channel);
		call->disconnect_cause = Q931_IE_C_CV_TEMPORARY_FAILURE;
		q931_send_disconnect(call, call->dlc,
			call->disconnect_cause);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive(call, error_indication);
	} else if (call->state == U25_OVERLAP_RECEIVING) {
		q931_call_stop_timer(call, T302);
		q931_call_disconnect_channel(call->channel);
		q931_send_disconnect(call, call->dlc,
			Q931_IE_C_CV_TEMPORARY_FAILURE);
		q931_call_start_timer(call, T305);
		q931_call_set_state(call, U11_DISCONNECT_REQUEST);
		q931_call_primitive(call, error_indication);
	} else {
	}
}

void q931_call_dl_establish_confirm(struct q931_call *call)
{
	if (call->state == N10_ACTIVE ||
	    call->state == U10_ACTIVE) {
		q931_call_stop_timer(call, T309);
		q931_send_status(call, call->dlc,
			Q931_IE_C_CV_NORMAL_UNSPECIFIED);
	}
}

void q931_call_dl_release_indication(struct q931_call *call)
{
	switch (call->state) {
	case N0_NULL_STATE:
	case U0_NULL_STATE:
		// Do nothing
	break;

	case U1_CALL_INITIATED:
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
		q931_call_stop_any_timer(call);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, error_indication);
	break;

	case U2_OVERLAP_SENDING:
		q931_call_stop_timer(call, T304);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, setup_confirm); // ERROR
	break;

	case N10_ACTIVE:
		if (q931_call_timer_running(call, T309)) {
			q931_call_stop_timer(call, T309);
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_call_primitive(call, release_indication);
		} else {
			q931_call_start_timer(call, T309);

			// FIXME
			if (connect(call->dlc->socket, NULL, 0) < 0) {
				return;
			}

			q931_dl_establish_confirm(call->dlc);
		}
	break;

	case U10_ACTIVE:

	break;

	case U25_OVERLAP_RECEIVING:
		q931_call_stop_timer(call, T302);
		q931_call_release_channel(call->channel);
		q931_call_set_state(call, U0_NULL_STATE);
		q931_intf_del_call(call);
		q931_call_primitive(call, release_indication); // ERROR
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
			q931_call_release_channel(call->channel);
			q931_call_set_state(call, N0_NULL_STATE);
			q931_intf_del_call(call);
			q931_call_primitive(call, release_indication);
		}
	break;
	}
}

void q931_call_dl_release_confirm(struct q931_call *call)
{
	q931_call_stop_timer(call, T309);

	if (!q931_timer_pending(&call->T322)) {
		q931_send_status_enquiry(call, call->dlc);

		q931_call_start_timer(call, T322);

		// NOTE 1: T322 MUST BE STOPPED ON RECEIPT OF A
		//         CLEARING MESSAGE
	}
}

void q931_dispatch_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt)
{
	report_call(call, LOG_DEBUG, "%s dispatched to CALL %d\n",
		q931_message_type_to_text(message_type),
		call->call_reference);

	switch (message_type) {
	case Q931_MT_ALERTING:
		q931_handle_alerting(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_handle_call_proceeding(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT:
		q931_handle_connect(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
		q931_handle_connect_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_PROGRESS:
		q931_handle_progress(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SETUP:
		q931_handle_setup(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SETUP_ACKNOWLEDGE:
		q931_handle_setup_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_DISCONNECT:
		q931_handle_disconnect(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE:
		q931_handle_release(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_handle_release_complete(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RESTART:
		q931_handle_restart(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RESTART_ACKNOWLEDGE:
		q931_handle_restart_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_STATUS:
		q931_handle_status(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_STATUS_ENQUIRY:
		q931_handle_status_enquiry(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_USER_INFORMATION:
		q931_handle_user_information(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SEGMENT:
		q931_handle_segment(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_CONGESTION_CONTROL:
		q931_handle_congestion_control(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_INFORMATION:
		q931_handle_info(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_FACILITY:
		q931_handle_facility(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_NOTIFY:
		q931_handle_notify(call, dlc, ies, ies_cnt);
	break;


	case Q931_MT_HOLD:
		q931_handle_hold(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_ACKNOWLEDGE:
		q931_handle_hold_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_REJECT:
		q931_handle_hold_reject(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE:
		q931_handle_retrieve(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_ACKNOWLEDGE:
		q931_handle_retrieve_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_REJECT:
		q931_handle_retrieve_reject(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RESUME:
		q931_handle_resume(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_ACKNOWLEDGE:
		q931_handle_resume_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_REJECT:
		q931_handle_resume_reject(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND:
		q931_handle_suspend(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_ACKNOWLEDGE:
		q931_handle_suspend_acknowledge(call, dlc, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_REJECT:
		q931_handle_suspend_reject(call, dlc, ies, ies_cnt);
	break;

	default:
		q931_call_unrecognized_message(call);
	break;
	}
}

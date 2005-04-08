#ifndef _CALL_H
#define _CALL_H

#include <string.h>

#include "callref.h"
#include "timer.h"
#include "list.h"
#include "ie.h"

#define report_call(call, lvl, format, arg...)				\
	(call)->intf->lib->report((lvl), format, ## arg)

#define q931_call_start_timer(call, timer)		\
	do {						\
		q931_start_timer_delta(			\
			(call)->intf->lib,		\
			&(call)->timer,			\
			(call)->intf->timer);	\
		report_call(call, LOG_DEBUG,		\
			"%s:%d Timer %s started\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_call_stop_timer(call, timer)		\
	do {						\
		q931_stop_timer(&(call)->timer);	\
		report_call(call, LOG_DEBUG,		\
			"%s:%d Timer %s stopped\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_call_timer_running(call, timer)		\
		q931_timer_pending(&(call)->timer)	\

enum q931_call_direction
{
	Q931_CALL_DIRECTION_OUTBOUND	= 0x0,
	Q931_CALL_DIRECTION_INBOUND	= 0x1,
};

enum q931_call_state
{
	U0_NULL_STATE,
	U1_CALL_INITIATED,
	U2_OVERLAP_SENDING,
	U3_OUTGOING_CALL_PROCEEDING,
	U4_CALL_DELIVERED,
	U6_CALL_PRESENT,
	U7_CALL_RECEIVED,
	U8_CONNECT_REQUEST,
	U9_INCOMING_CALL_PROCEEDING,
	U10_ACTIVE,
	U11_DISCONNECT_REQUEST,
	U12_DISCONNECT_INDICATION,
	U15_SUSPEND_REQUEST,
	U17_RESUME_REQUEST,
	U19_RELEASE_REQUEST,
	U25_OVERLAP_RECEIVING,

	N0_NULL_STATE,
	N1_CALL_INITIATED,
	N2_OVERLAP_SENDING,
	N3_OUTGOING_CALL_PROCEEDING,
	N4_CALL_DELIVERED,
	N6_CALL_PRESENT,
	N7_CALL_RECEIVED,
	N8_CONNECT_REQUEST,
	N9_INCOMING_CALL_PROCEEDING,
	N10_ACTIVE,
	N11_DISCONNECT_REQUEST,
	N12_DISCONNECT_INDICATION,
	N15_SUSPEND_REQUEST,
	N17_RESUME_REQUEST,
	N19_RELEASE_REQUEST,
	N22_CALL_ABORT,
	N25_OVERLAP_RECEIVING
};

enum q931_setup_mode
{
	Q931_SETUP_POINT_TO_POINT,
	Q931_SETUP_BROADCAST,
};

enum q931_tone_type
{
	Q931_TONE_DIAL,
	Q931_TONE_BUSY,
	Q931_TONE_HANGUP,
	Q931_TONE_FAILURE,
};

#define Q931_MAX_DIGITS 20

struct q931_call
{
	struct list_head calls_node;

	struct q931_interface *intf;

	// TODO: Use a HASH for improved scalability
	struct list_head ces;
	struct q931_ces *preselected_ces;
	struct q931_ces *selected_ces;
	struct q931_dlc *dlc;

	enum q931_call_direction direction;
	q931_callref call_reference;

	enum q931_call_state state;

	char calling_number[Q931_MAX_DIGITS + 1];
	char called_number[Q931_MAX_DIGITS + 1];
	int sending_complete;
	int broadcast_setup;
	int tones_option;
	int T303_fired;
	int T308_fired;
	enum q931_ie_cause_value disconnect_cause;
	enum q931_ie_cause_value release_with_cause;
	__u8 release_diagnostics[10]; // FIXME???

	struct q931_channel *channel; // Maybe we should have a channel mask, instead

	void *pvt;

	struct q931_timer T301;
	struct q931_timer T302;
	struct q931_timer T303;
	struct q931_timer T304;
	struct q931_timer T305;
	struct q931_timer T306;
	struct q931_timer T307;
	struct q931_timer T308;
	struct q931_timer T309;
	struct q931_timer T310;
	struct q931_timer T312;
	struct q931_timer T313;
	struct q931_timer T314;
	struct q931_timer T316;
	struct q931_timer T317;
	struct q931_timer T318;
	struct q931_timer T319;
	struct q931_timer T320;
	struct q931_timer T321;
	struct q931_timer T322;

	void (*alerting_indication)(struct q931_call *call);
	void (*connect_indication)(struct q931_call *call);
	void (*disconnect_indication)(struct q931_call *call);
	void (*error_indication)(struct q931_call *call); // TE
	void (*info_indication)(struct q931_call *call);
	void (*more_info_indication)(struct q931_call *call);
	void (*notify_indication)(struct q931_call *call);
	void (*proceeding_indication)(struct q931_call *call);
	void (*progress_indication)(struct q931_call *call);
	void (*reject_indication)(struct q931_call *call);
	void (*release_confirm)(struct q931_call *call);//TE
	void (*release_indication)(struct q931_call *call);
	void (*resume_confirm)(struct q931_call *call);//TE
	void (*resume_indication)(struct q931_call *call);
	void (*setup_complete_indication)(struct q931_call *call);//TE
	void (*setup_confirm)(struct q931_call *call);
	void (*setup_indication)(struct q931_call *call);
	void (*status_indication)(struct q931_call *call);
	void (*suspend_confirm)(struct q931_call *call);//TE
	void (*suspend_indication)(struct q931_call *call);
	void (*timeout_indication)(struct q931_call *call);
};

struct q931_call *q931_alloc_call_in(
	struct q931_interface *intf,
	struct q931_dlc *dlc,
	int call_reference,
	int broadcast_setup);

struct q931_call *q931_alloc_call_out(
	struct q931_interface *intf);

void q931_free_call(struct q931_call *call);

void q931_call_set_state(
	struct q931_call *call,
	enum q931_call_state state);

void q931_call_dl_establish_indication(struct q931_call *call);
void q931_call_dl_establish_confirm(struct q931_call *call);
void q931_call_dl_release_indication(struct q931_call *call);
void q931_call_dl_release_confirm(struct q931_call *call);

void q931_alerting_request(struct q931_call *call);
void q931_disconnect_request(struct q931_call *call,
	enum q931_ie_cause_value cause);
void q931_info_request(struct q931_call *call);
void q931_more_info_request(struct q931_call *call);
void q931_notify_request(struct q931_call *call);
void q931_proceeding_request(struct q931_call *call);
void q931_progress_request(struct q931_call *call);
void q931_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause);
void q931_release_request(struct q931_call *call);
void q931_resume_request(struct q931_call *call);
void q931_resume_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause);

void q931_resume_response(struct q931_call *call);
void q931_setup_complete_request(struct q931_call *call);
void q931_setup_request(struct q931_call *call);
void q931_setup_response(struct q931_call *call);
void q931_status_enquiry_request(struct q931_call *call);
void q931_suspend_reject_request(struct q931_call *call,
	enum q931_ie_cause_value cause);

void q931_suspend_response(struct q931_call *call);
void q931_suspend_request(struct q931_call *call);

void q931_int_alerting_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_connect_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_call_proceeding_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_release_complete_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_info_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_progress_indication(struct q931_call *call, struct q931_ces *ces);
void q931_int_release_indication( struct q931_call *call, struct q931_ces *ces);

void q931_dispatch_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt);

struct q931_call *q931_find_call_by_reference(
	struct q931_interface *intf,
	enum q931_call_direction direction,
	q931_callref call_reference);

static inline void q931_call_set_calling_number(
	struct q931_call *call,
	const char *calling_number)
{
	strncpy(call->calling_number, calling_number,
		sizeof(call->calling_number));
	call->called_number[sizeof(call->calling_number)-1]='\0';
}

static inline void q931_call_set_called_number(
	struct q931_call *call,
	const char *called_number)
{
	strncpy(call->called_number, called_number,
		sizeof(call->called_number));
	call->called_number[sizeof(call->called_number)-1]='\0';
}

#endif

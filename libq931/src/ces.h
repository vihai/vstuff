#ifndef _CES_H
#define _CES_H

#include "timer.h"

#define report_ces(ces, lvl, format, arg...)	\
	(ces)->call->interface->lib->report((lvl), format, ## arg)

enum q931_ces_state
{
	I0_NULL_STATE,
	I7_CALL_RECEIVED,
	I8_CONNECT_REQUEST,
	I9_INCOMING_CALL_PROCEEDING,
	I19_RELEASE_REQUEST,
	I25_OVERLAP_RECEIVING,
};

// Connection endpoint suffix
struct q931_ces
{
	struct list_head node;

	struct q931_call *call;
	struct q931_dlc *dlc;
	enum q931_ces_state state;
};

void q931_ces_dispatch_message(
	struct q931_ces *ces,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt);

struct q931_ces *q931_ces_alloc(struct q931_call *call);
void q931_ces_del(struct q931_ces *ces);
void q931_ces_free(struct q931_ces *ces);

void q931_ces_dl_establish_indication(struct q931_ces *ces);
void q931_ces_dl_release_indication(struct q931_ces *ces);

void q931_ces_alerting_request(struct q931_ces *ces);
void q931_ces_connect_request(struct q931_ces *ces);
void q931_ces_call_proceeding_request(struct q931_ces *ces);
void q931_ces_setup_ack_request(struct q931_ces *ces);
void q931_ces_release_request(struct q931_ces *ces);
void q931_ces_info_request(struct q931_ces *ces);

#endif

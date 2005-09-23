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

#ifndef _LIBQ931_CES_H
#define _LIBQ931_CES_H

#include <libq931/timer.h>

#define q931_ces_start_timer(ces, timer)		\
	do {						\
		q931_start_timer_delta(			\
			(ces)->call->intf->lib,		\
			&(ces)->timer,			\
			(ces)->call->intf->timer);	\
		report_ces(ces, LOG_DEBUG,		\
			"%s:%d Timer %s started\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_ces_stop_timer(ces, timer)		\
	do {						\
		q931_stop_timer(&(ces)->timer);	\
		report_ces(ces, LOG_DEBUG,		\
			"%s:%d Timer %s stopped\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_ces_timer_running(ces, timer)		\
		q931_timer_pending(&(ces)->timer)	\

#define report_ces(ces, lvl, format, arg...)	\
	(ces)->call->intf->lib->report((lvl), format, ## arg)

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

	struct q931_timer T304;
	struct q931_timer T308;
	struct q931_timer T322;

	int T308_fired;

	int senq_cause_97_98_received;
	int senq_cnt;
};

const char *q931_ces_state_to_text(enum q931_call_state state);

#ifdef Q931_PRIVATE

void q931_ces_dispatch_message(
	struct q931_ces *ces,
	struct q931_message *msg);

struct q931_ces *q931_ces_alloc(
	struct q931_call *call,
	struct q931_dlc *dlc);

void q931_ces_del(struct q931_ces *ces);
void q931_ces_free(struct q931_ces *ces);

void q931_ces_dl_establish_indication(
	struct q931_ces *ces);
void q931_ces_dl_release_indication(
	struct q931_ces *ces);
void q931_ces_dl_establish_confirm(
	struct q931_ces *ces);
void q931_ces_dl_release_confirm(
	struct q931_ces *ces);

void q931_ces_alerting_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_connect_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_call_proceeding_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_setup_ack_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_release_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_info_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);
void q931_ces_status_enquiry_request(
	struct q931_ces *ces,
	const struct q931_ies *user_ies);

#endif

#endif

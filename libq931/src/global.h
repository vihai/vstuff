#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "intf.h"
#include "chanset.h"

#define q931_global_primitive(gc, primitive, arg...)		\
	do {							\
		if ((gc)->intf->lib->primitive)			\
			(gc)->intf->lib->primitive(gc, ## arg);	\
	} while(0);

enum q931_global_state
{
	Q931_GLOBAL_STATE_NULL,
	Q931_GLOBAL_STATE_RESTART_REQUEST,
	Q931_GLOBAL_STATE_RESTART
};

struct q931_interface;
struct q931_global_call
{
	struct q931_interface *intf;

	enum q931_global_state state;

	struct q931_timer T316;
	struct q931_timer T317;
	int T317_expired;

	int restart_retransmit_count;
	int restart_responded;
	int restart_acknowledged;
	int restart_request_count;

	struct q931_chanset restart_reqd_chans;
	struct q931_chanset restart_acked_chans;
};


#ifdef Q931_PRIVATE

#define q931_global_start_timer(cr, timer)		\
	do {						\
		q931_start_timer_delta(			\
			(cr)->intf->lib,		\
			&(cr)->timer,			\
			(cr)->intf->timer);		\
		report_intf((cr)->intf, LOG_DEBUG,	\
			"%s:%d Timer %s started\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_global_stop_timer(cr, timer)		\
	do {						\
		q931_stop_timer(&(cr)->timer);		\
		report_intf((cr)->intf, LOG_DEBUG,	\
			"%s:%d Timer %s stopped\n",	\
			__FILE__,__LINE__,		\
			#timer);			\
	} while(0)

#define q931_global_timer_running(cr, timer)		\
		q931_timer_pending(&(cr)->timer)


void q931_dispatch_global_message(
	struct q931_global_call *gc,
	struct q931_message *msg);

void q931_global_restart_confirm(
	struct q931_global_call *gc,
	struct q931_call *call);

#endif
#endif

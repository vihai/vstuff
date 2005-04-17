#ifndef _MESSAGE_H
#define _MESSAGE_H

#ifdef Q931_PRIVATE

#include "call.h"
#include "dlc.h"

#define report_msg(msg, lvl, format, arg...)				\
	(msg)->dlc->intf->lib->report((lvl), format, ## arg)

struct q931_ie;
struct q931_message
{
	__u8 raw[512];
	int rawlen;

	struct q931_dlc *dlc;
	enum q931_message_type message_type;
	q931_callref callref;
	int callref_len;
	enum q931_callref_flag callref_direction;

	struct q931_ie ies[260];
	int ies_cnt;
};

#endif
#endif

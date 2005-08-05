#ifndef _LIBQ931_MESSAGE_H
#define _LIBQ931_MESSAGE_H

#include "call.h"
#include "dlc.h"
#include "ies.h"

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

	struct q931_ies ies;
};

#ifdef Q931_PRIVATE

#define report_msg(msg, lvl, format, arg...)				\
	(msg)->dlc->intf->lib->report((lvl), format, ## arg)

static inline void q931_message_clone(
	struct q931_message *dst,
	struct q931_message *src)
{
	memcpy(dst, src, sizeof(*dst));
}

#endif
#endif

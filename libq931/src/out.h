#ifndef _LIBQ931_OUT_H
#define _LIBQ931_OUT_H

#include "call.h"
#include "ie_progind.h"


int q931_send_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

int q931_send_message_bc(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

int q931_global_send_message(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *ies);

#endif

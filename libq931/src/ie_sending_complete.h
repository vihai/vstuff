#ifndef _LIBQ931_IE_SENDING_COMPLETE_H
#define _LIBQ931_IE_SENDING_COMPLETE_H

#include "ie.h"

void q931_ie_sending_complete_register(
	const struct q931_ie_type *type);

int q931_ie_sending_complete_check(
	const struct q931_ie *ie,
	const struct q931_message *msg);

int q931_ie_sending_complete_write_to_buf(
        const struct q931_ie *generic_ie,
        void *buf,
        int max_size);

#endif

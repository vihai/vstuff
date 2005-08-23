#ifndef _LIBQ931_IE_SENDING_COMPLETE_H
#define _LIBQ931_IE_SENDING_COMPLETE_H

#include "ie.h"

struct q931_ie_sending_complete
{
	struct q931_ie ie;
};

struct q931_ie_sending_complete *q931_ie_sending_complete_alloc(void);
struct q931_ie *q931_ie_sending_complete_alloc_abstract(void);

#ifdef Q931_PRIVATE

void q931_ie_sending_complete_register(
	const struct q931_ie_type *type);

int q931_ie_sending_complete_read_from_buf(
	struct q931_ie *ie,
	const struct q931_message *msg,
	int pos,
	int len);

int q931_ie_sending_complete_write_to_buf(
        const struct q931_ie *generic_ie,
        void *buf,
        int max_size);

void q931_ie_sending_complete_dump(
	const struct q931_ie *ie,
	const struct q931_message *msg,
	const char *prefix);

#endif
#endif

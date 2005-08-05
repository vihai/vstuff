#define Q931_PRIVATE

#include "ie_sending_complete.h"

void q931_ie_sending_complete_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

int q931_ie_sending_complete_check(
	const struct q931_ie *ie,
	const struct q931_message *msg)
{
	// TODO

	return TRUE;
}

int q931_ie_sending_complete_write_to_buf(
        struct q931_ie *generic_ie,
        void *buf,
        int max_size)
{
 *(__u8 *)buf = Q931_IE_SENDING_COMPLETE;

 return 1;
}

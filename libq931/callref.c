#include <assert.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include <libq931/callref.h>

void q931_make_callref(
	void *void_buf,
	int size,
	q931_callref callref,
	enum q931_callref_flag direction)
{
	assert(void_buf);
	assert(direction == Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE ||
	       direction == Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE);

	int i;
	__u8 *buf = void_buf;

	for (i=0; i<size; i++) {
		buf[i] = callref & (0xFF << ((size-i-1) * 8));

		if (i == 0 &&
		    direction == Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE)
			buf[i] |= 0x80;
	}
}


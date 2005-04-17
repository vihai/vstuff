#ifndef _CALLREF_H
#define _CALLREF_H

enum q931_callref_flag
{
	Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE = 0x0,
	Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE = 0x1,
};

typedef signed long q931_callref;

void q931_make_callref(
	void *void_buf,
	int size,
	q931_callref callref,
	enum q931_callref_flag direction);

#endif

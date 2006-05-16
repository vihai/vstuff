/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <assert.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include <libq931/callref.h>

void q931_make_callref(
	void *buf,
	int len,
	q931_callref callref,
	enum q931_callref_flag direction)
{
	assert(buf);
	assert(direction == Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE ||
	       direction == Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE);
	assert(len <= sizeof(q931_callref));

	union { q931_callref cr; __u8 raw[sizeof(q931_callref)]; } cu;

	cu.cr = callref;

	int i;
	for (i=0; i<len; i++) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
		((__u8 *)buf)[i] = cu.raw[len - 1 - i];
#else
		((__u8 *)buf)[i] = cu.raw[i];
#endif
	}

	if (direction == Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE)
		*((__u8 *)buf) |= 0x80;
}


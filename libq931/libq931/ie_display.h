/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#ifndef _LIBQ931_IE_DISPLAY_H
#define _LIBQ931_IE_DISPLAY_H

#include <libq931/ie.h>

/************************* Display ***************************/

struct q931_ie_display
{
	struct q931_ie ie;

	__u8 text[83];
};

struct q931_ie_display *q931_ie_display_alloc(void);
struct q931_ie *q931_ie_display_alloc_abstract(void);

#ifdef Q931_PRIVATE

void q931_ie_display_register(
	const struct q931_ie_type *type);

int q931_ie_display_read_from_buf(
	struct q931_ie *ie,
	const struct q931_message *msg,
	int pos,
	int len);

int q931_ie_display_write_to_buf(
	const struct q931_ie *generic_ie,
        void *buf,
	int max_size);

void q931_ie_display_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif

#endif

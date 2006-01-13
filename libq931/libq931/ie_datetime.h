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

#ifndef _LIBQ931_IE_DATETIME_H
#define _LIBQ931_IE_DATETIME_H

#include <libq931/ie.h>

/************************* Date/Time *************************/

struct q931_ie_datetime
{
	struct q931_ie ie;

	time_t time;
};

struct q931_ie_datetime *q931_ie_datetime_alloc(void);
struct q931_ie *q931_ie_datetime_alloc_abstract(void);

#ifdef Q931_PRIVATE

struct q931_ie_datetime_onwire_3
{
	__u8 year;
	__u8 month;
	__u8 day;
	__u8 hour;
	__u8 minute;
	__u8 second;
} __attribute__ ((__packed__));

void q931_ie_datetime_register(
	const struct q931_ie_class *ie_class);

int q931_ie_datetime_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_datetime_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_datetime_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif

#endif

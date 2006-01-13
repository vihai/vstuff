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

#ifndef _LIBQ931_IE_NOTIFICATION_INDICATOR_H
#define _LIBQ931_IE_NOTIFICATION_INDICATOR_H

#include <libq931/ie.h>

/******************** Notification Indicator *********************/

enum q931_ie_notification_indicator_description
{
	Q931_IE_NI_D_USER_SUSPENDED		= 0x00,
	Q931_IE_NI_D_USER_RESUMED		= 0x01,
	Q931_IE_NI_D_BEARER_SERVICE_CHANGE	= 0x02,
};

struct q931_ie_notification_indicator
{
	struct q931_ie ie;

	enum q931_ie_notification_indicator_description description;
};

struct q931_ie_notification_indicator *q931_ie_notification_indicator_alloc(void);
struct q931_ie *q931_ie_notification_indicator_alloc_abstract(void);

#ifdef Q931_PRIVATE

struct q931_ie_notification_indicator_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 description:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 description:7;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

void q931_ie_notification_indicator_register(
	const struct q931_ie_type *type);

int q931_ie_notification_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_notification_indicator_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_notification_indicator_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif

#endif

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

#ifndef _LIBQ931_IE_PROGRESS_INDICATOR_H
#define _LIBQ931_IE_PROGRESS_INDICATOR_H

#include <libq931/ie.h>

/************************* Progress Indicator ***************************/

enum q931_ie_progress_indicator_coding_standard
{
	Q931_IE_PI_CS_CCITT		= 0x0,
	Q931_IE_PI_CS_RESERVED		= 0x1,
	Q931_IE_PI_CS_NATIONAL		= 0x2,
	Q931_IE_PI_CS_NETWORK_SPECIFIC	= 0x3,
};

enum q931_ie_progress_indicator_location
{
	Q931_IE_PI_L_USER					= 0x0,
	Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_LOCAL_USER		= 0x1,
	Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_LOCAL_USER		= 0x2,
	Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_REMOTE_USER		= 0x4,
	Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_REMOTE_USER	= 0x5,
	Q931_IE_PI_L_INTERNATIONAL_NETWORK			= 0x7,
	Q931_IE_PI_L_NETWORK_BEYOND_INTERNETWORKING_POINT	= 0xa,
};

enum q931_ie_progress_indicator_progress_description
{
	Q931_IE_PI_PD_CALL_NOT_END_TO_END			= 0x1,
	Q931_IE_PI_PD_DESTINATION_ADDRESS_IS_NON_ISDN		= 0x2,
	Q931_IE_PI_PD_ORIGINATION_ADDRESS_IS_NON_ISDN		= 0x3,
	Q931_IE_PI_PD_CALL_HAS_RETURNED_TO_THE_ISDN		= 0x4,
	Q931_IE_PI_PD_IN_BAND_INFORMATION			= 0x8,
};

struct q931_ie_progress_indicator
{
	struct q931_ie ie;

	enum q931_ie_progress_indicator_coding_standard
		coding_standard;
	enum q931_ie_progress_indicator_location
		location;
	enum q931_ie_progress_indicator_progress_description
		progress_description;
};

struct q931_ie_progress_indicator *q931_ie_progress_indicator_alloc(void);
struct q931_ie *q931_ie_progress_indicator_alloc_abstract(void);

enum q931_ie_progress_indicator_location
	q931_ie_progress_indicator_location(
		const struct q931_call *call);

#ifdef Q931_PRIVATE

struct q931_ie_progress_indicator_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 coding_standard:2;
	__u8 :1;
	__u8 location:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 location:4;
	__u8 :1;
	__u8 coding_standard:2;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

struct q931_ie_progress_indicator_onwire_4
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 progress_description:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 progress_description:7;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

#endif

void q931_ie_progress_indicator_register(
	const struct q931_ie_class *ie_class);

int q931_ie_progress_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_progress_indicator_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_progress_indicator_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif

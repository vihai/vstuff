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

#ifndef _LIBQ931_IE_HIGH_LAYER_COMPATIBILIT_H
#define _LIBQ931_IE_HIGH_LAYER_COMPATIBILIY_H

#include <libq931/ie.h>

enum q931_ie_high_layer_compatibility_coding_standard
{
	Q931_IE_HLC_CS_CCITT		= 0x0,
	Q931_IE_HLC_CS_RESERVED		= 0x1,
	Q931_IE_HLC_CS_NATIONAL		= 0x2,
	Q931_IE_HLC_CS_NETWORK_SPECIFIC	= 0x3,
};

enum q931_ie_high_layer_compatibility_interpretation
{
	Q931_IE_HLC_P_FIRST	= 0x4,
};

enum q931_ie_high_layer_compatibility_presentation_method
{
	Q931_IE_HLC_PM_HIGH_LAYER_PROTOCOL_PROFILE	= 0x01,
};

enum q931_ie_high_layer_compatibility_characteristics_identification
{
	Q931_IE_HLC_CI_TELEPHONY				= 0x01,
	Q931_IE_HLC_CI_FACSIMILE_G2_G3				= 0x04,
	Q931_IE_HLC_CI_FACSIMILE_G4_CALSS1			= 0x21,
	Q931_IE_HLC_CI_TELETEX_F184_FACSIMILE_G4_CLASS2_3	= 0x24,
	Q931_IE_HLC_CI_TELETEX_F220				= 0x28,
	Q931_IE_HLC_CI_TELETEX_F200				= 0x31,
	Q931_IE_HLC_CI_VIDEOTEX					= 0x32,
	Q931_IE_HLC_CI_TELEX					= 0x35,
	Q931_IE_HLC_CI_X400					= 0x38,
	Q931_IE_HLC_CI_X200					= 0x45,
	Q931_IE_HLC_CI_RESERVED_FOR_MAINTENANCE			= 0x5e,
	Q931_IE_HLC_CI_RESERVED_FOR_MANAGEMENT			= 0x5f,
	Q931_IE_HLC_CI_RESERVED					= 0x7f,
};

struct q931_ie_high_layer_compatibility
{
	struct q931_ie ie;

	enum q931_ie_high_layer_compatibility_coding_standard
		coding_standard;
	enum q931_ie_high_layer_compatibility_interpretation
		interpretation;
	enum q931_ie_high_layer_compatibility_presentation_method
		presentation_method;
	enum q931_ie_high_layer_compatibility_characteristics_identification
		characteristics_identification;
	enum q931_ie_high_layer_compatibility_characteristics_identification
		extended_characteristics_identification;
};

struct q931_ie_high_layer_compatibility *q931_ie_high_layer_compatibility_alloc(void);
struct q931_ie *q931_ie_high_layer_compatibility_alloc_abstract(void);

#ifdef Q931_PRIVATE

struct q931_ie_high_layer_compatibility_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 coding_standard:2;
	__u8 presentation_method:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 presentation_method:2;
	__u8 interpretation:3;
	__u8 coding_standard:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_high_layer_compatibility_onwire_4
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 characteristics_identification:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 characteristics_identification:7;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_high_layer_compatibility_onwire_4a
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 extended_characteristics_identification:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 extended_characteristics_identification:7;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

void q931_ie_high_layer_compatibility_register(
	const struct q931_ie_class *ie_class);

int q931_ie_high_layer_compatibility_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_high_layer_compatibility_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_high_layer_compatibility_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif
#endif

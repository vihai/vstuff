#ifndef _LIBQ931_IE_HLC_H
#define _LIBQ931_IE_HLC_H

#include "ie.h"

enum q931_ie_high_layer_compatibility_coding_standard
{
	Q931_IE_HLC_CS_CCITT	= 0x0,
	Q931_IE_HLC_CS_RESERVED	= 0x1,
	Q931_IE_HLC_CS_NATIONAL	= 0x2,
	Q931_IE_HLC_CS_SPECIFIC	= 0x3,
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

void q931_ie_high_layer_compatibility_register(
	const struct q931_ie_type *type);

int q931_ie_high_layer_compatibility_check(
	const struct q931_ie *ie,
	const struct q931_message *msg);


#endif
#endif

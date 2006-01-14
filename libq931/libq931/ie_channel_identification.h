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

#ifndef _LIBQ931_IE_CHANNEL_IDENTIFICATION_H
#define _LIBQ931_IE_CHANNEL_IDENTIFICATION_H

#include <libq931/ie.h>
#include <libq931/chanset.h>
#include <libq931/intf.h>

/*********************** Channel Identification *************************/

enum q931_ie_channel_identification_interface_id_present
{
	Q931_IE_CI_IIP_IMPLICIT	= 0x0,
	Q931_IE_CI_IIP_EXPLICIT	= 0x1
};

enum q931_ie_channel_identification_interface_type
{
	Q931_IE_CI_IT_BASIC	= 0x0,
	Q931_IE_CI_IT_PRIMARY	= 0x1
};

enum q931_ie_channel_identification_preferred_exclusive
{
	Q931_IE_CI_PE_PREFERRED	= 0x0,
	Q931_IE_CI_PE_EXCLUSIVE	= 0x1
};

enum q931_ie_channel_identification_d_channel_indicator
{
	Q931_IE_CI_DCI_IS_NOT_D_CHAN	= 0x0,
	Q931_IE_CI_DCI_IS_D_CHAN	= 0x1
};

enum q931_ie_channel_identification_info_channel_selection_bra
{
	Q931_IE_CI_ICS_BRA_NO_CHANNEL	= 0x0,
	Q931_IE_CI_ICS_BRA_B1		= 0x1,
	Q931_IE_CI_ICS_BRA_B2		= 0x2,
	Q931_IE_CI_ICS_BRA_ANY		= 0x3
};

enum q931_ie_channel_identification_info_channel_selection_pra
{
	Q931_IE_CI_ICS_PRA_NO_CHANNEL	= 0x0,
	Q931_IE_CI_ICS_PRA_INDICATED	= 0x1,
	Q931_IE_CI_ICS_PRA_RESERVED	= 0x2,
	Q931_IE_CI_ICS_PRA_ANY		= 0x3
};

enum q931_ie_channel_identification_coding_standard
{
	Q931_IE_CI_CS_CCITT		= 0x0,
	Q931_IE_CI_CS_RESERVED		= 0x1,
	Q931_IE_CI_CS_NATIONAL		= 0x2,
	Q931_IE_CI_CS_NETWORK		= 0x3
};

enum q931_ie_channel_identification_number_map
{
	Q931_IE_CI_NM_NUMBER		= 0x0,
	Q931_IE_CI_NM_MAP		= 0x1
};

enum q931_ie_channel_identification_element_type
{
	Q931_IE_CI_ET_B			= 0x3,
	Q931_IE_CI_ET_H0		= 0x6,
	Q931_IE_CI_ET_H11		= 0x8,
	Q931_IE_CI_ET_H12		= 0x9
};

struct q931_ie_channel_identification
{
	struct q931_ie ie;

	enum q931_ie_channel_identification_interface_id_present
		interface_id_present;
	enum q931_ie_channel_identification_interface_type
		interface_type;
	enum q931_ie_channel_identification_preferred_exclusive
		preferred_exclusive;
	enum q931_ie_channel_identification_d_channel_indicator
		d_channel_indicator;
	enum q931_ie_channel_identification_coding_standard
		coding_standard;

	struct q931_chanset chanset;

//	enum q931_ie_channel_identification_number_map
//	enum q931_ie_channel_identification_element_type
};

struct q931_ie_channel_identification *
	q931_ie_channel_identification_alloc(void);
struct q931_ie *
	q931_ie_channel_identification_alloc_abstract(void);

static inline enum q931_ie_channel_identification_interface_type
	q931_ie_channel_identification_intftype(
		struct q931_interface *intf)
{
	if (intf->type == Q931_INTF_TYPE_PRA)
		return Q931_IE_CI_IT_PRIMARY;
	else
		return Q931_IE_CI_IT_BASIC;
}

int q931_ie_channel_identification_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_channel_identification_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_channel_identification_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#ifdef Q931_PRIVATE

struct q931_ie_channel_identification_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 interface_id_present:1;
	__u8 interface_type:1;
	__u8 :1;
	__u8 preferred_exclusive:1;
	__u8 d_channel_indicator:1;
	__u8 info_channel_selection:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 info_channel_selection:2;
	__u8 d_channel_indicator:1;
	__u8 preferred_exclusive:1;
	__u8 :1;
	__u8 interface_type:1;
	__u8 interface_id_present:1;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_channel_identification_onwire_3b
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 interface_identifier:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 interface_identifier:7;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

struct q931_ie_channel_identification_onwire_3c
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 coding_standard:2;
	__u8 number_map:1;
	__u8 channel_type_map_identifier_type:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 channel_type_map_identifier_type:4;
	__u8 number_map:1;
	__u8 coding_standard:2;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

struct q931_ie_channel_identification_onwire_3d
{
	union
	{
	struct
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 channel_number:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 channel_number:7;
	__u8 ext:1;
#endif
	};

	__u8 slot_map;
	};
} __attribute__ ((__packed__));

void q931_ie_channel_identification_register(
	const struct q931_ie_class *ie_class);

#endif
#endif

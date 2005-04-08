
#ifndef _IE_CHANID_H
#define _IE_CHANID_H

#include "ie.h"

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
#endif

int q931_ie_channel_identification_check(
	struct q931_call *call,
	struct q931_ie *ie);
int q931_append_ie_channel_identification_bra(void *buf,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	enum q931_ie_channel_identification_info_channel_selection_bra chan_id);
int q931_append_ie_channel_identification_pra(void *buf,
	enum q931_ie_channel_identification_info_channel_selection_pra selection,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	int chan_id);

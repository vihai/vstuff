
#ifndef _Q931_IE_CHANID_H
#define _Q931_IE_CHANID_H

#include "q931_ie.h"

/*********************** Channel Identification *************************/

enum q931_ie_channel_identification_interface_id_present
{
	Q931_IE_CI_IIP_IMPLICIT	= 0x0,
	Q931_IE_CI_IIP_EXPLICIT	= 0x1
};

enum q931_ie_channel_identification_interface_type
{
	Q931_IE_CI_IT_BASIC	= 0x0,
	Q931_IE_CI_IT_OTHER	= 0x1
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

enum q931_ie_channel_identification_info_channel_selection_bri
{
	Q931_IE_CI_ICS_BRI_NO_CHANNEL	= 0x00,
	Q931_IE_CI_ICS_BRI_B1		= 0x01,
	Q931_IE_CI_ICS_BRI_B2		= 0x02,
	Q931_IE_CI_ICS_BRI_ANY		= 0x03
};

enum q931_ie_channel_identification_info_channel_selection_pri
{
	Q931_IE_CI_ICS_PRI_NO_CHANNEL	= 0x00,
	Q931_IE_CI_ICS_PRI_INDICATED	= 0x01,
	Q931_IE_CI_ICS_PRI_RESERVED	= 0x02,
	Q931_IE_CI_ICS_PRI_ANY		= 0x03
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

int q931_append_ie_channel_identification_any(void *buf);
int q931_append_ie_channel_identification(void *buf,
        enum q931_ie_channel_identification_info_channel_selection_bri chan_id);

#endif

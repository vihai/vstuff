
#ifndef _IE_PROGIND_H
#define _IE_PROGIND_H

#include "ie.h"

/************************* Progress Indicator ***************************/

enum q931_ie_progress_indicator_coding_standard
{
	Q931_IE_PI_CS_CCITT	= 0x0,
	Q931_IE_PI_CS_RESERVED	= 0x1,
	Q931_IE_PI_CS_NATIONAL	= 0x2,
	Q931_IE_PI_CS_SPECIFIC	= 0x3,
};

enum q931_ie_progress_indicator_location
{
	Q931_IE_PI_LOCATION_USER				= 0x0,
	Q931_IE_PI_LOCATION_PRIVATE_NET_SERVING_LOCAL_USER	= 0x1,
	Q931_IE_PI_LOCATION_PUBLIC_NETWORK_SERVING_LOCAL_USER	= 0x2,
	Q931_IE_PI_LOCATION_PUBLIC_NETWORK_SERVING_REMOTE_USER	= 0x4,
	Q931_IE_PI_LOCATION_PRIVATE_NETWORK_SERVING_REMOTE_USER	= 0x5,
	Q931_IE_PI_LOCATION_INTERNATIONAL_NETWORK		= 0x7,
	Q931_IE_PI_LOCATION_NETWORK_BEYOND_INTERNETWORKING_POINT= 0xa,
};

enum q931_ie_progress_indicator_progress_description
{
	Q931_IE_PI_PD_CALL_NOT_END_TO_END			= 0x1,
	Q931_IE_PI_PD_DESTINATION_ADDRESS_IS_NON_ISDN		= 0x2,
	Q931_IE_PI_PD_ORIGINATION_ADDRESS_IS_NON_ISDN		= 0x3,
	Q931_IE_PI_PD_CALL_HAS_RETURNED_TO_THE_ISDN		= 0x4,
	Q931_IE_PI_PD_IN_BAND_INFORMATION			= 0x8,
};

struct q931_ie_progress_indicator_onwire_3_4
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

#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext2:1;
	__u8 progress_description:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 progress_description:7;
	__u8 ext2:1;
#endif
} __attribute__ ((__packed__));

#endif

int q931_append_ie_progress_indicator(void *buf,
	enum q931_ie_progress_indicator_location location,
	enum q931_ie_progress_indicator_progress_description description);

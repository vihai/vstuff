/*********************** Called Party Number *************************/

#ifndef _Q931_IE_CDPN_H
#define _Q931_IE_CDPN_H

#include "q931_ie.h"

enum q931_ie_called_party_number_type_of_number
{
	Q931_IE_CDPN_TON_UNKNOWN		= 0x0,
	Q931_IE_CDPN_TON_INTERNATIONAL		= 0x1,
	Q931_IE_CDPN_TON_NATIONAL		= 0x2,
	Q931_IE_CDPN_TON_NETWORK_SPECIFIC	= 0x3,
	Q931_IE_CDPN_TON_SUBSCRIBER		= 0x4,
	Q931_IE_CDPN_TON_ABBREVIATED		= 0x6,
	Q931_IE_CDPN_TON_RESERVED_FOR_EXT	= 0x7
};

enum q931_ie_called_party_number_numbering_plan
{
	Q931_IE_CDPN_NP_UNKNOWN			= 0x0,
	Q931_IE_CDPN_NP_ISDN_TELEPHONY		= 0x1,
	Q931_IE_CDPN_NP_DATA			= 0x3,
	Q931_IE_CDPN_NP_TELEX			= 0x4,
	Q931_IE_CDPN_NP_NATIONAL_STANDARD	= 0x8,
	Q931_IE_CDPN_NP_PRIVATE			= 0x9,
	Q931_IE_CDPN_NP_RESERVED_FOR_EXT	= 0xf
};

struct q931_ie_called_party_number_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 type_of_number:3;
	__u8 numbering_plan_identificator:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 numbering_plan_identificator:4;
	__u8 type_of_number:3;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

int q931_append_ie_called_party_number(void *buf, const char *called_number);

#endif

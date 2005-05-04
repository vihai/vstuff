
#ifndef _IE_RESTIND_H
#define _IE_RESTIND_H

#include "ie.h"

/*********************** Restart Indicator *************************/

enum q931_ie_restart_indicator_class
{
	Q931_IE_RI_C_INDICATED		= 0x0,
	Q931_IE_RI_C_SIGNLE_INTERFACE	= 0x6,
	Q931_IE_RI_C_ALL_INTERFACES	= 0x7
};

struct q931_ie_restart_indicator_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 :4;
	__u8 restart_class:3;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 restart_class:3;
	__u8 :4;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

int q931_ie_restart_indicator_check(
	const struct q931_message *msg,
	const struct q931_ie *ie);

int q931_append_ie_restart_indicator(void *buf,
	enum q931_ie_restart_indicator_class restart_class);

#endif

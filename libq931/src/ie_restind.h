
#ifndef _LIBQ931_IE_RESTIND_H
#define _LIBQ931_IE_RESTIND_H

#include "ie.h"

/*********************** Restart Indicator *************************/

enum q931_ie_restart_indicator_class
{
	Q931_IE_RI_C_INDICATED		= 0x0,
	Q931_IE_RI_C_SINGLE_INTERFACE	= 0x6,
	Q931_IE_RI_C_ALL_INTERFACES	= 0x7
};

struct q931_ie_restart_indicator
{
	struct q931_ie ie;

	enum q931_ie_restart_indicator_class ri_class;
};

#ifdef Q931_PRIVATE

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

void q931_ie_restart_indicator_init(
	struct  q931_ie_restart_indicator *ie);

void q931_ie_restart_indicator_register(
	const struct q931_ie_type *type);

int q931_ie_restart_indicator_check(
	const struct q931_ie *ie,
	const struct q931_message *msg);

int q931_ie_restart_indicator_write_to_buf(
	const struct q931_ie *generic_ie,
        void *buf,
	int max_size);

#endif
#endif

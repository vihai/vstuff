/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <locale.h>
#include <iconv.h>

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>

#include "chan_vgsm.h"
#include "util.h"
#include "sms.h"
#include "operators.h"
#include "bcd.h"
#include "7bit.h"
#include "gsm_charset.h"

const char *vgsm_sms_alphabet_to_text(enum vgsm_sms_dcs_alphabet value)
{
	switch(value) {
	case VGSM_SMS_DCS_ALPHABET_DEFAULT:
		return "default";
	case VGSM_SMS_DCS_ALPHABET_8_BIT_DATA:
		return "8-bit data";
	case VGSM_SMS_DCS_ALPHABET_UCS2:
		return "ucs2";
	case VGSM_SMS_DCS_ALPHABET_RESERVED:
		return "reserved";
	}

	return "*INVALID*";
};

const char *vgsm_sms_udh_iei_to_text(int value)
{
	switch(value) {
	case VGSM_SMS_UDH_IEI_CONCATENATED_SMS:
		return "Concatenated Message";
	case VGSM_SMS_UDH_IEI_SPECIAL_SMS_INDICATION:
		return "Special SMS Message Indication";
	case VGSM_SMS_UDH_IEI_RESERVED:
		return "Reserved";
	case VGSM_SMS_UDH_IEI_NOT_USED:
		return "Not used";
	case VGSM_SMS_UDH_IEI_APPLICATION_PORT_8:
		return "Application port addressing scheme, 8 bit address";
	case VGSM_SMS_UDH_IEI_APPLICATION_PORT_16:
		return "Application port addressing scheme, 16 bit address";
	case VGSM_SMS_UDH_IEI_SMCC_CONTROL_PARAMETERS:
		return "SMSC Control Parameters";
	case VGSM_SMS_UDH_IEI_UDH_SOURCE_INDICATOR:
		return "UDH Source Indicator";
	case VGSM_SMS_UDH_IEI_CONCATENATED_16BIT:
		return "Concatenated short message, 16-bit reference number";
	case VGSM_SMS_UDH_IEI_WCMP:
		return "Wireless Control Message Protocol";
	}

	if (value >= 0x0a && value <= 0x6f)
		return "Reserved for future use";
	else if (value < 0x80)
		return "SIM Toolink Security Header";
	else if (value < 0xa0)
		return "SME to SME specific use";
	else if (value < 0xc0)
		return "Reserved for future use";
	else if (value < 0xe0)
		return "SC specific use";

	return "Reserved for future use";
};

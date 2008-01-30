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

#ifndef _VGSM_SMS_H
#define _VGSM_SMS_H

#include "number.h"

#define VGSM_SMS_MT_DELIVER		0x0
#define VGSM_SMS_MT_DELIVER_REPORT	0x0
#define VGSM_SMS_MT_SUBMIT		0x1
#define VGSM_SMS_MT_SUBMIT_REPORT	0x1
#define VGSM_SMS_MT_STATUS_REPORT	0x2
#define VGSM_SMS_MT_COMMAND		0x2
#define VGSM_SMS_MT_RESERVED		0x3

enum vgsm_sms_udh_iei
{
	VGSM_SMS_UDH_IEI_CONCATENATED_SMS		= 0x00,
	VGSM_SMS_UDH_IEI_SPECIAL_SMS_INDICATION		= 0x01,
	VGSM_SMS_UDH_IEI_RESERVED			= 0x02,
	VGSM_SMS_UDH_IEI_NOT_USED			= 0x03,
	VGSM_SMS_UDH_IEI_APPLICATION_PORT_8		= 0x04,
	VGSM_SMS_UDH_IEI_APPLICATION_PORT_16		= 0x05,
	VGSM_SMS_UDH_IEI_SMCC_CONTROL_PARAMETERS	= 0x06,
	VGSM_SMS_UDH_IEI_UDH_SOURCE_INDICATOR		= 0x07,
	VGSM_SMS_UDH_IEI_CONCATENATED_16BIT		= 0x08,
	VGSM_SMS_UDH_IEI_WCMP				= 0x09,
};

struct vgsm_type_of_address
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 type_of_number:3;
	__u8 numbering_plan_identificator:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 numbering_plan_identificator:4;
	__u8 type_of_number:3;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_protocol_identifier
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 :6;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :6;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_protocol_identifier_00
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 interworking:1;
	__u8 :5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :5;
	__u8 interworking:1;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_protocol_identifier_000
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 interworking:1;
	__u8 sm_al_proto:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 sm_al_proto:5;
	__u8 interworking:1;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_protocol_identifier_001
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 interworking:1;
	__u8 telematic_device:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 telematic_device:5;
	__u8 interworking:1;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

enum vgsm_sms_protocol_type
{
	VGSM_SMS_PT_INTERWORKING	= 0x00,
	VGSM_SMS_PT_MESSAGE_TYPE	= 0x01,
	VGSM_SMS_PT_SC_SPECIFIC_USE	= 0x03,
};

enum vgsm_sms_message_type
{
	VGSM_SMS_MT_SHORT_TYPE_0		= 0x00,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_1	= 0x01,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_2	= 0x02,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_3	= 0x03,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_4	= 0x04,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_5	= 0x05,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_6	= 0x06,
	VGSM_SMS_MT_REPLACE_SHORT_TYPE_7	= 0x07,
	VGSM_SMS_MT_RETURN_CALL			= 0x1f,
	VGSM_SMS_MT_ME_DATA_DOWNLOAD		= 0x3d,
	VGSM_SMS_MT_ME_DEPERSONALIZATION	= 0x3e,
	VGSM_SMS_MT_SIM_DATA_DOWNLOAD		= 0x3f,
};

enum vgsm_vpf
{
	VGSM_VPF_NOT_PRESENT			= 0x0,
	VGSM_VPF_ENHANCED_FORMAT		= 0x1,
	VGSM_VPF_RELATIVE_FORMAT		= 0x2,
	VGSM_VPF_ASBOLUTE_FORMAT		= 0x3,
};

struct vgsm_sms_protocol_identifier_01
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 message_type:6;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 message_type:6;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_protocol_identifier_11
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 protocol_type:2;
	__u8 :8;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :8;
	__u8 protocol_type:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

enum vgsm_sms_dcs_alphabet
{
	VGSM_SMS_DCS_ALPHABET_DEFAULT		= 0x0,
	VGSM_SMS_DCS_ALPHABET_8_BIT_DATA	= 0x1,
	VGSM_SMS_DCS_ALPHABET_UCS2		= 0x2,
	VGSM_SMS_DCS_ALPHABET_RESERVED		= 0x3
};

struct vgsm_sms_data_coding_scheme
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 :4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :4;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_data_coding_scheme_00xx
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:2;
	__u8 compressed:1;
	__u8 has_class:1;
	__u8 message_alphabet:2;
	__u8 message_class:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 message_class:2;
	__u8 message_alphabet:2;
	__u8 has_class:1;
	__u8 compressed:1;
	__u8 coding_group:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_data_coding_scheme_110x
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:3;
	__u8 store:1;
	__u8 mwi_active:1;
	__u8 :1;
	__u8 indication_type:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 indication_type:2;
	__u8 :1;
	__u8 mwi_active:1;
	__u8 store:1;
	__u8 coding_group:3;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_data_coding_scheme_1110
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 mwi_active:1;
	__u8 :1;
	__u8 indication_type:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 indication_type:2;
	__u8 :1;
	__u8 mwi_active:1;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_data_coding_scheme_1111
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 :1;
	__u8 message_coding:1;
	__u8 message_class:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 message_class:2;
	__u8 message_coding:1;
	__u8 :1;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

const char *vgsm_sms_alphabet_to_text(enum vgsm_sms_dcs_alphabet value);
const char *vgsm_sms_udh_iei_to_text(int value);


#endif

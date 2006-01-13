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

#ifndef _LIBQ931_IE_CAUSE_H
#define _LIBQ931_IE_CAUSE_H

#include <sys/socket.h>

#include <libq931/ie.h>
#include <libq931/ies.h>
#include <libq931/intf.h>

enum q931_ie_cause_coding_standard
{
	Q931_IE_C_CS_CCITT	= 0x0,
	Q931_IE_C_CS_RESERVED	= 0x1,
	Q931_IE_C_CS_NATIONAL	= 0x2,
	Q931_IE_C_CS_SPECIFIC	= 0x3,
};

enum q931_ie_cause_location
{
	Q931_IE_C_L_USER					= 0x0,
	Q931_IE_C_L_PRIVATE_NETWORK_SERVING_LOCAL_USER		= 0x1,
	Q931_IE_C_L_PUBLIC_NETWORK_SERVING_LOCAL_USER		= 0x2,
	Q931_IE_C_L_TRANSIT_NETWORK				= 0x3,
	Q931_IE_C_L_PUBLIC_NETWORK_SERVING_REMOTE_USER		= 0x4,
	Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER		= 0x5,
	Q931_IE_C_L_INTERNATIONAL_NETWORK			= 0x7,
	Q931_IE_C_L_NETWORK_BEYOND_INTERNETWORKING_POINT	= 0xa,
};

enum q931_ie_cause_value
{
	Q931_IE_C_CV_UNALLOCATED_NUMBER				= 1,
	Q931_IE_C_CV_NO_ROUTE_TO_SPECIFIED_TRANSIT_NETWORK	= 2,
	Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION			= 3,
	Q931_IE_C_CV_CHANNEL_UNACCEPTABLE			= 6,
	Q931_IE_C_CV_CALL_BEING_DELIVERED			= 7,
	Q931_IE_C_CV_NORMAL_CALL_CLEARING			= 16,
	Q931_IE_C_CV_USER_BUSY					= 17,
	Q931_IE_C_CV_NO_USER_RESPONDING				= 18,
	Q931_IE_C_CV_NO_ANSWER_FROM_USER			= 19,
	Q931_IE_C_CV_CALL_REJECTED				= 21,
	Q931_IE_C_CV_NUMBER_CHANGED				= 22,
	Q931_IE_C_CV_CALL_REJECTED_DUE_TO_ACR			= 24,
	Q931_IE_C_CV_NON_SELECTED_USER_CLEARING			= 26,
	Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER			= 27,
	Q931_IE_C_CV_INVALID_NUMBER_FORMAT			= 28,
	Q931_IE_C_CV_FACILITY_REJECTED				= 29,
	Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY			= 30,
	Q931_IE_C_CV_NORMAL_UNSPECIFIED				= 31,

	Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE		= 34,
	Q931_IE_C_CV_NETWORK_OUT_OF_ORDER			= 38,
	Q931_IE_C_CV_TEMPORARY_FAILURE				= 41,
	Q931_IE_C_CV_SWITCHING_EQUIPMENT_CONGESTION		= 42,
	Q931_IE_C_CV_ACCESS_INFORMATION_DISCARDED		= 43,
	Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE	= 44,
	Q931_IE_C_CV_RESOURCES_UNAVAILABLE			= 47,

	Q931_IE_C_CV_QUALITY_OF_SERVICE_UNAVAILABLE		= 49,
	Q931_IE_C_CV_REQUESTED_FACILITY_NOT_SUBSCRIBED		= 50,
	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_AUTHORIZED		= 57,
	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_PRESENTLY_AVAILABLE	= 58,
	Q931_IE_C_CV_SERVICE_OR_OPTION_NOT_AVAILABLE		= 63,

	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_IMPLEMENTED		= 65,
	Q931_IE_C_CV_CHANNEL_TYPE_NOT_IMPLEMENTED		= 66,
	Q931_IE_C_CV_REQUESTED_FACILITY_NOT_IMPLEMENTED		= 69,
	Q931_IE_C_CV_ONLY_RESTRICTED_DIGITAL_AVAILABLE		= 70,
	Q931_IE_C_CV_SERVICE_OR_OPTION_NOT_IMPLEMENTED		= 79,

	Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE		= 81,
	Q931_IE_C_CV_IDENTIFIED_CHANNEL_DOES_NOT_EXIST		= 82,
	Q931_IE_C_CV_SUSPENDED_CALL_EXISTS_BUT_NOT_THIS		= 83,
	Q931_IE_C_CV_CALL_IDENITY_IN_USE			= 84,
	Q931_IE_C_CV_NO_CALL_SUSPENDED				= 85,
	Q931_IE_C_CV_CALL_IDENTIFIED_HAS_BEEN_CLEARED		= 86,
	Q931_IE_C_CV_INCOMPATIBLE_DESTINATION			= 88,
	Q931_IE_C_CV_INVALID_TRANSIT_NETWORK_SELECTION		= 91,
	Q931_IE_C_CV_INVALID_MESSAGE_UNSPECIFIED		= 95,

	Q931_IE_C_CV_MANDATORY_INFORMATION_ELEMENT_IS_MISSING	= 96,
	Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED	= 97,
	Q931_IE_C_CV_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_STATE	= 98,
	Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT		= 99,
	Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS	= 100,
	Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE	= 101,
	Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY			= 102,
	Q931_IE_C_CV_PROTOCOL_ERROR_UNSPECIFIED			= 111,

	Q931_IE_C_CV_INTERWORKING_UNSPECIFIED			= 127,
};

enum q931_ie_cause_value_recommendation
{
	Q931_IE_C_R_Q931	= 0x00,
	Q931_IE_C_R_X21		= 0x03,
	Q931_IE_C_R_X25		= 0x04,
};

struct q931_ie_cause
{
	struct q931_ie ie;

	enum q931_ie_cause_coding_standard
		coding_standard;
	enum q931_ie_cause_location
		location;
	enum q931_ie_cause_value
		value;
	enum q931_ie_cause_value_recommendation
		recommendation;

	__u8 diagnostics[27];
	int diagnostics_len;
};

struct q931_ie_cause *q931_ie_cause_alloc(void);
struct q931_ie *q931_ie_cause_alloc_abstract(void);

enum q931_ie_cause_location q931_ie_cause_location(
	enum q931_call_direction dir,
	enum q931_interface_network_role network_role,
	enum lapd_role role);

static inline enum q931_ie_cause_location q931_ie_cause_location_call(
	struct q931_call *call)
{
	return q931_ie_cause_location(
			call->direction,
			call->intf->network_role,
			call->intf->role);
}

static inline enum q931_ie_cause_location q931_ie_cause_location_gc(
	struct q931_global_call *gc)
{
	return q931_ie_cause_location(
			Q931_CALL_DIRECTION_INBOUND,
			gc->intf->network_role,
			gc->intf->role);
}

#ifdef Q931_PRIVATE

struct q931_ie_cause_value_info
{
	enum q931_ie_cause_value value;
	const char *name;
};

struct q931_ie_cause_onwire_3
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
} __attribute__ ((__packed__));

struct q931_ie_cause_onwire_3a
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 recommendation:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 recommendation:7;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

struct q931_ie_cause_onwire_4
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 cause_value:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 cause_value:7;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

enum q931_ie_cause_diag_condition
{
	Q931_IE_C_D_C_UNKNOWN	= 0x0,
	Q931_IE_C_D_C_PERMANENT	= 0x1,
	Q931_IE_C_D_C_TRANSIENT	= 0x2
};

struct q931_ie_cause_diag_1_2
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext1:1;
	__u8 attribute_number:7;

	__u8 ext2:1;
	__u8 :5;
	__u8 condition:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 condition:2;
	__u8 :5;
	__u8 ext2:1;

	__u8 attribute_number:7;
	__u8 ext1:1;
#endif
} __attribute__ ((__packed__));

void q931_ie_cause_value_infos_init();

void q931_ie_cause_register(
	const struct q931_ie_class *ie_class);

int q931_ie_cause_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_cause_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_cause_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

int q931_ies_contain_cause(
	const struct q931_ies *ies,
	enum q931_ie_cause_value cause);

#endif
#endif

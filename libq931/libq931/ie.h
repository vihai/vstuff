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

#ifndef _LIBQ931_IE_H
#define _LIBQ931_IE_H

#include <stdlib.h>
#include <linux/types.h>
#include <endian.h>

#include <libq931/util.h>
#include <libq931/msgtype.h>
#include <libq931/dlc.h>

struct q931_ie_type;

struct q931_ie
{
	const struct q931_ie_type *type;

	int refcnt;
};

static inline void q931_ie_get(struct q931_ie *ie)
{
	ie->refcnt++;
}

static inline void q931_ie_put(struct q931_ie *ie)
{
	ie->refcnt--;
	if (ie->refcnt == 0)
		free(ie);
}

struct q931_ie_onwire
{
	__u8 id;
	__u8 len;
	__u8 data[0];
} __attribute__ ((__packed__));

enum q931_ie_id
{
	// Single octect IEs
	Q931_IE_SHIFT				= 0x90,

	Q931_IE_CONGESTION_LEVEL		= 0xb0,
	Q931_IE_MORE_DATA			= 0xa0,
	Q931_IE_SENDING_COMPLETE		= 0xa1,
	Q931_IE_REPEAT_INDICATOR		= 0xd0,

	// Variable length IE
	Q931_IE_SEGMENTED_MESSAGE		= 0x00,
	Q931_IE_CHANGE_STATUS			= 0x01,
	Q931_IE_SPECIAL				= 0x02,
	Q931_IE_CONNECTED_ADDRESS		= 0x0c,
	Q931_IE_EXTENDED_FACILITY		= 0x0d,

	Q931_IE_BEARER_CAPABILITY		= 0x04,
	Q931_IE_CAUSE				= 0x08,
	Q931_IE_CALL_IDENTITY			= 0x10,
	Q931_IE_CALL_STATE			= 0x14,
	Q931_IE_CHANNEL_IDENTIFICATION		= 0x18,
	Q931_IE_FACILITY			= 0x1c,
	Q931_IE_PROGRESS_INDICATOR		= 0x1e,
	Q931_IE_NETWORK_SPECIFIC_FACILITIES	= 0x20,
	Q931_IE_ENDPOINT_ID			= 0x26,
	Q931_IE_NOTIFICATION_INDICATOR		= 0x27,
	Q931_IE_DISPLAY				= 0x28,
	Q931_IE_DATETIME			= 0x29,
	Q931_IE_KEYPAD_FACILITY			= 0x2c,
	Q931_IE_CALL_STATUS			= 0x2d,
	Q931_IE_UPDATE				= 0x31,
	Q931_IE_INFO_REQUEST			= 0x32,
	Q931_IE_SIGNAL				= 0x34,
	Q931_IE_SWITCHHOOK			= 0x36,
	Q931_IE_FEATURE_ACTIVATION		= 0x38,
	Q931_IE_FEATURE_INDICATION		= 0x39,
	Q931_IE_INFORMATION_RATE		= 0x40,
	Q931_IE_END_TO_END_TRANSIT_DELAY	= 0x42,
	Q931_IE_TRANSIT_DELAY_SELECTION_AND_INDICATION	= 0x43,

	Q931_IE_PACKET_LAYER_BINARY_PARAMETERS	= 0x44,
	Q931_IE_PACKET_LAYER_WINDOW_SIZE	= 0x45,
	Q931_IE_PACKET_SIZE			= 0x46,
	Q931_IE_CLOSED_USER_GROUP		= 0x47,
	Q931_IE_REVERSE_CHARGE_INDICATION	= 0x4a,
	Q931_IE_CONNECTED_NUMBER		= 0x4c,

	Q931_IE_CALLING_PARTY_NUMBER		= 0x6c,
	Q931_IE_CALLING_PARTY_SUBADDRESS	= 0x6d,
	Q931_IE_CALLED_PARTY_NUMBER		= 0x70,
	Q931_IE_CALLED_PARTY_SUBADDRESS		= 0x71,
	Q931_IE_ORIGINAL_CALLED_NUMBER		= 0x73,
	Q931_IE_REDIRECTING_NUMBER		= 0x74,
	Q931_IE_REDIRECTING_SUBADDRESS		= 0x75,
	Q931_IE_REDIRECTION_NUMBER		= 0x76,
	Q931_IE_REDIRECTION_SUBADDRESS		= 0x77,

	Q931_IE_TRANSIT_NETWORK_SELECTION	= 0x78,
	Q931_IE_RESTART_INDICATOR		= 0x79,
	Q931_IE_USER_USER_FACILITY		= 0x7a,
	Q931_IE_LOW_LAYER_COMPATIBILITY		= 0x7c,
	Q931_IE_USER_USER			= 0x7e,
	Q931_IE_HIGH_LAYER_COMPATIBILITY	= 0x7d,

	Q931_IE_ESCAPE_FOR_EXTENSION		= 0x7f
};

#define Q931_NT_UNKNOWN	0
#define Q931_NT_ETSI	(1 << 0)

struct q931_call;
struct q931_message;
struct q931_ie_type
{
	int max_len;
	int max_occur;
	int network_type;
	enum q931_ie_id id;
	const char *name;
	void (*init)(
		const struct q931_ie_type *type);

	struct q931_ie *(*alloc)(void);

	int (*read_from_buf)(
		struct q931_ie *ie,
		const struct q931_message *msg,
		int pos,
		int len);

	int (*write_to_buf)(
		const struct q931_ie *ie,
		void *buf, int max_size);

	void (*dump)(
		const struct q931_ie *ie,
		void (*report)(int level, const char *format, ...),
		const char *prefix);
};

enum q931_ie_direction
{
	Q931_IE_DIR_N_TO_U,
	Q931_IE_DIR_U_TO_N,
	Q931_IE_DIR_BOTH
};

enum q931_ie_presence
{
	Q931_IE_OPTIONAL,
	Q931_IE_MANDATORY,
};

struct q931_ie_type_per_mt
{
	enum q931_message_type message_type;
	enum q931_ie_id ie_id;
	enum q931_ie_direction direction;
	enum q931_ie_presence presence;
};

static inline int q931_is_so_ie(__u8 ie_id)
{
	return ie_id & 0x80;
}

#define Q931_SOIE_TYPE1 1
#define Q931_SOIE_TYPE2 2

static inline int q931_get_so_ie_type(__u8 ie_id)
{
	return ((ie_id & 0x70) == 0x20) ? Q931_SOIE_TYPE2 : Q931_SOIE_TYPE1;
}

#define Q931_SINGLE_OCTET_ID_MASK 0xF0
#define Q931_SINGLE_OCTET_VALUE_MASK 0x0F

static inline int q931_get_so_ie_id(__u8 ie_id)
{
	return (q931_get_so_ie_type(ie_id) == Q931_SOIE_TYPE1) ?
			(ie_id & Q931_SINGLE_OCTET_ID_MASK) :
			ie_id;
}

static inline int q931_get_so_ie_type2_value(__u8 ie_id)
{
	return ie_id & Q931_SINGLE_OCTET_VALUE_MASK;
}

#define Q931_IE_COMPREHENSION_REQUIRED_MASK 0xF0

static inline int q931_ie_comprehension_required(__u8 ie_id)
{
	return ie_id & Q931_IE_COMPREHENSION_REQUIRED_MASK;
}

void q931_ie_types_init();
const struct q931_ie_type *q931_get_ie_type(
	enum q931_ie_id id);
const struct q931_ie_type_per_mt *q931_get_ie_type_per_mt(
	enum q931_message_type message_type,
	enum q931_ie_id ie_id);

#endif

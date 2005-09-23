/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#ifndef _LIBQ931_IE_CALL_STATE_H
#define _LIBQ931_IE_CALL_STATE_H

#include <libq931/ie.h>

enum q931_ie_call_state_coding_standard
{
	Q931_IE_CS_CS_CCITT	= 0x0,
	Q931_IE_CS_CS_RESERVED	= 0x1,
	Q931_IE_CS_CS_NATIONAL	= 0x2,
	Q931_IE_CS_CS_SPECIFIC	= 0x3,
};

enum q931_ie_call_state_value
{
	Q931_IE_CS_N0_NULL_STATE		= 0x00,
	Q931_IE_CS_N1_CALL_INITIATED		= 0x01,
	Q931_IE_CS_N2_OVERLAP_SENDING		= 0x02,
	Q931_IE_CS_N3_OUTGOING_CALL_PROCEEDING	= 0x03,
	Q931_IE_CS_N4_CALL_DELIVERED		= 0x04,
	Q931_IE_CS_N6_CALL_PRESENT		= 0x06,
	Q931_IE_CS_N7_CALL_RECEIVED		= 0x07,
	Q931_IE_CS_N8_CONNECT_REQUEST		= 0x08,
	Q931_IE_CS_N9_INCOMING_CALL_PROCEEDING	= 0x09,
	Q931_IE_CS_N10_ACTIVE			= 0x0A,
	Q931_IE_CS_N11_DISCONNECT_REQUEST	= 0x0B,
	Q931_IE_CS_N12_DISCONNECT_INDICATION	= 0x0C,
	Q931_IE_CS_N15_SUSPEND_REQUEST		= 0x0F,
	Q931_IE_CS_N17_RESUME_REQUEST		= 0x11,
	Q931_IE_CS_N19_RELEASE_REQUEST		= 0x13,
	Q931_IE_CS_N22_CALL_ABORT		= 0x16,
	Q931_IE_CS_N25_OVERLAP_RECEIVING	= 0x19,

	Q931_IE_CS_U0_NULL_STATE		= 0x00,
	Q931_IE_CS_U1_CALL_INITIATED		= 0x01,
	Q931_IE_CS_U2_OVERLAP_SENDING		= 0x02,
	Q931_IE_CS_U3_OUTGOING_CALL_PROCEEDING	= 0x03,
	Q931_IE_CS_U4_CALL_DELIVERED		= 0x04,
	Q931_IE_CS_U6_CALL_PRESENT		= 0x06,
	Q931_IE_CS_U7_CALL_RECEIVED		= 0x07,
	Q931_IE_CS_U8_CONNECT_REQUEST		= 0x08,
	Q931_IE_CS_U9_INCOMING_CALL_PROCEEDING	= 0x09,
	Q931_IE_CS_U10_ACTIVE			= 0x0A,
	Q931_IE_CS_U11_DISCONNECT_REQUEST	= 0x0B,
	Q931_IE_CS_U12_DISCONNECT_INDICATION	= 0x0C,
	Q931_IE_CS_U15_SUSPEND_REQUEST		= 0x0F,
	Q931_IE_CS_U17_RESUME_REQUEST		= 0x11,
	Q931_IE_CS_U19_RELEASE_REQUEST		= 0x13,
	Q931_IE_CS_U25_OVERLAP_RECEIVING	= 0x19,

	Q931_IE_CS_REST0			= 0x00,
	Q931_IE_CS_REST1			= 0x3d,
	Q931_IE_CS_REST2			= 0x3e,
};

struct q931_ie_call_state
{
	struct q931_ie ie;

	enum q931_ie_call_state_coding_standard
		coding_standard;
	enum q931_ie_call_state_value
		value;
};

struct q931_ie_call_state *q931_ie_call_state_alloc(void);
struct q931_ie *q931_ie_call_state_alloc_abstract(void);

#ifdef Q931_PRIVATE

struct q931_ie_call_state_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_standard:2;
	__u8 value:6;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 value:6;
	__u8 coding_standard:2;
#endif
} __attribute__ ((__packed__));

void q931_ie_call_state_register(
	const struct q931_ie_type *type);

int q931_ie_call_state_read_from_buf(
	struct q931_ie *ie,
	const struct q931_message *msg,
	int pos,
	int len);

int q931_ie_call_state_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_call_state_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#endif
#endif

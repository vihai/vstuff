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

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/ie_call_state.h>

static const struct q931_ie_type *ie_type;

void q931_ie_call_state_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_call_state *q931_ie_call_state_alloc(void)
{
	struct q931_ie_call_state *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_call_state_alloc_abstract(void)
{
	return &q931_ie_call_state_alloc()->ie;
}

int q931_ie_call_state_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_call_state *ie =
		container_of(abstract_ie,
			struct q931_ie_call_state, ie);

	int nextoct = 0;

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_call_state_onwire_3 *oct_3 =
		(struct q931_ie_call_state_onwire_3 *)
		(msg->rawies + pos + (nextoct++));

	if (oct_3->coding_standard != Q931_IE_CS_CS_CCITT) {
		report_msg(msg, LOG_ERR, "coding stanrdard != CCITT\n");
		return FALSE;
	}

	ie->coding_standard = oct_3->coding_standard;
	ie->value = oct_3->value;

	return TRUE;
}

int q931_ie_call_state_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_call_state *ie =
		container_of(generic_ie, struct q931_ie_call_state, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_CALL_STATE;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_call_state_onwire_3 *oct_3 =
	  (struct q931_ie_call_state_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->coding_standard = ie->coding_standard;
	oct_3->value = ie->value;
	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_call_state_coding_standard_to_text(
	enum q931_ie_call_state_coding_standard coding_standard)
{
	switch(coding_standard) {
	case Q931_IE_CS_CS_CCITT:
		return "CCITT";
	case Q931_IE_CS_CS_RESERVED:
		return "Reserved";
	case Q931_IE_CS_CS_NATIONAL:
		return "National";
	case Q931_IE_CS_CS_SPECIFIC:
		return "Specific";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_call_state_value_to_text(
	enum q931_ie_call_state_value value)
{
	switch(value) {
	case Q931_IE_CS_N0_NULL_STATE:
		return "0_NULL_STATE";
	case Q931_IE_CS_N1_CALL_INITIATED:
		return "1_CALL_INITIATED";
	case Q931_IE_CS_N2_OVERLAP_SENDING:
		return "2_OVERLAP_SENDING";
	case Q931_IE_CS_N3_OUTGOING_CALL_PROCEEDING:
		return "3_OUTGOING_CALL_PROCEEDING";
	case Q931_IE_CS_N4_CALL_DELIVERED:
		return "4_CALL_DELIVERED";
	case Q931_IE_CS_N6_CALL_PRESENT:
		return "6_CALL_PRESENT";
	case Q931_IE_CS_N7_CALL_RECEIVED:
		return "7_CALL_RECEIVED";
	case Q931_IE_CS_N8_CONNECT_REQUEST:
		return "8_CONNECT_REQUEST";
	case Q931_IE_CS_N9_INCOMING_CALL_PROCEEDING:
		return "9_INCOMING_CALL_PROCEEDING";
	case Q931_IE_CS_N10_ACTIVE:
		return "10_ACTIVE";
	case Q931_IE_CS_N11_DISCONNECT_REQUEST:
		return "11_DISCONNECT_REQUEST";
	case Q931_IE_CS_N12_DISCONNECT_INDICATION:
		return "12_DISCONNECT_INDICATION";
	case Q931_IE_CS_N15_SUSPEND_REQUEST:
		return "15_SUSPEND_REQUEST";
	case Q931_IE_CS_N17_RESUME_REQUEST:
		return "17_RESUME_REQUEST";
	case Q931_IE_CS_N19_RELEASE_REQUEST:
		return "19_RELEASE_REQUEST";
	case Q931_IE_CS_N22_CALL_ABORT:
		return "22_CALL_ABORT";
	case Q931_IE_CS_N25_OVERLAP_RECEIVING:
		return "25_OVERLAP_RECEIVING";
	case Q931_IE_CS_REST1:
		return "REST0";
	case Q931_IE_CS_REST2:
		return "REST0";
	default:
		return "*INVALID*";
	}
}

void q931_ie_call_state_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_call_state *ie =
		container_of(generic_ie, struct q931_ie_call_state, ie);

	report(LOG_DEBUG, "%sCoding standard = %s (%d)\n", prefix,
		q931_ie_call_state_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	report(LOG_DEBUG, "%sState value = %s (%d)\n", prefix,
		q931_ie_call_state_value_to_text(
			ie->value),
		ie->value);
}

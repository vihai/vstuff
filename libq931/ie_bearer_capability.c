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

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/ie_bearer_capability.h>

static const struct q931_ie_type *ie_type;

void q931_ie_bearer_capability_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_bearer_capability *q931_ie_bearer_capability_alloc(void)
{
	struct q931_ie_bearer_capability *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_bearer_capability_alloc_abstract(void)
{
	return &q931_ie_bearer_capability_alloc()->ie;
}

int q931_ie_bearer_capability_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_bearer_capability *ie =
		container_of(abstract_ie,
			struct q931_ie_bearer_capability, ie);

	int nextoct = 0;

	if (len < 2) {
		report_ie(abstract_ie, LOG_WARNING, "IE size < 2\n");
		return FALSE;
	}

	ie->user_information_layer_1_protocol = Q931_IE_BC_UIL1P_UNUSED;
	ie->user_information_layer_2_protocol = Q931_IE_BC_UIL2P_UNUSED;
	ie->user_information_layer_3_protocol = Q931_IE_BC_UIL3P_UNUSED;

	struct q931_ie_bearer_capability_onwire_3 *oct_3 =
		(struct q931_ie_bearer_capability_onwire_3 *)
		(buf + (nextoct++));

	if (!oct_3->ext) {
		report_ie(abstract_ie, LOG_WARNING, "IE oct 3 ext != 0\n");
		return FALSE;
	}

	ie->coding_standard =
		oct_3->coding_standard;
	ie->information_transfer_capability =
		oct_3->information_transfer_capability;

	struct q931_ie_bearer_capability_onwire_4 *oct_4 =
		(struct q931_ie_bearer_capability_onwire_4 *)
		(buf + (nextoct++));

	ie->transfer_mode =
		oct_4->transfer_mode;
	ie->information_transfer_rate =
		oct_4->information_transfer_rate;

	if (oct_4->ext)
		goto oct_4_end;

	struct q931_ie_bearer_capability_onwire_4a *oct_4a =
		(struct q931_ie_bearer_capability_onwire_4a *)
		(buf + (nextoct++));

	if (oct_4a->ext)
		goto oct_4_end;

	struct q931_ie_bearer_capability_onwire_4b *oct_4b =
		(struct q931_ie_bearer_capability_onwire_4b *)
		(buf + (nextoct++));

	if (!oct_4b->ext) {
		report_ie(abstract_ie, LOG_WARNING, "IE oct 4b ext != 0\n");
		return FALSE;
	}

oct_4_end:

	if (q931_ie_bearer_capability_oct_ident(
				*(__u8 *)(buf + nextoct)) ==
				Q931_IE_BC_LAYER_1_IDENT) {

		struct q931_ie_bearer_capability_onwire_5 *oct_5 =
			(struct q931_ie_bearer_capability_onwire_5 *)
			(buf + (nextoct++));

		ie->user_information_layer_1_protocol =
			oct_5->user_information_layer_1_protocol;

		if (oct_5->ext)
			goto oct_5_end;

		struct q931_ie_bearer_capability_onwire_5a *oct_5a =
			(struct q931_ie_bearer_capability_onwire_5a *)
			(buf + (nextoct++));

		if (oct_5a->ext)
			goto oct_5_end;

		int oct_5b_ext;

		if (oct_5->user_information_layer_1_protocol ==
			Q931_IE_BC_UIL1P_V110) {

			struct q931_ie_bearer_capability_onwire_5b1 *oct_5b1 =
				(struct q931_ie_bearer_capability_onwire_5b1 *)
				(buf + (nextoct++));

			oct_5b_ext = oct_5b1->ext;
		} else if (oct_5->user_information_layer_1_protocol ==
			Q931_IE_BC_UIL1P_V120) {

			struct q931_ie_bearer_capability_onwire_5b2 *oct_5b2 =
				(struct q931_ie_bearer_capability_onwire_5b2 *)
				(buf + (nextoct++));

			oct_5b_ext = oct_5b2->ext;
		} else {
			report_ie(abstract_ie, LOG_WARNING,
				"IE oct 5b ext != 0 and l1 != v110 or v120\n");
			return FALSE;
		}

		if (oct_5b_ext)
			goto oct_5_end;

		struct q931_ie_bearer_capability_onwire_5c *oct_5c =
			(struct q931_ie_bearer_capability_onwire_5c *)
			(buf + (nextoct++));

		if (oct_5c->ext)
			goto oct_5_end;

		struct q931_ie_bearer_capability_onwire_5d *oct_5d =
			(struct q931_ie_bearer_capability_onwire_5d *)
			(buf + (nextoct++));

		if (oct_5d->ext) {
			report_ie(abstract_ie, LOG_WARNING,
				"IE oct 5d ext != 0\n");
			return FALSE;
		}
oct_5_end:;
	}

	if (nextoct >= len)
		return TRUE;

	if (q931_ie_bearer_capability_oct_ident(
				*(__u8 *)(buf + nextoct)) ==
				Q931_IE_BC_LAYER_2_IDENT) {

		struct q931_ie_bearer_capability_onwire_6 *oct_6 =
			(struct q931_ie_bearer_capability_onwire_6 *)
			(buf + (nextoct++));

		if (oct_6->ext) {
			report_ie(abstract_ie, LOG_WARNING, "IE oct 6 ext != 0\n");
			return FALSE;
		}

		ie->user_information_layer_2_protocol =
			oct_6->user_information_layer_2_protocol;
	}

	if (nextoct >= len)
		return TRUE;

	if (q931_ie_bearer_capability_oct_ident(
				*(__u8 *)(buf + nextoct)) ==
				Q931_IE_BC_LAYER_3_IDENT) {

		struct q931_ie_bearer_capability_onwire_7 *oct_7 =
			(struct q931_ie_bearer_capability_onwire_7 *)
			(buf + (nextoct++));

		if (oct_7->ext) {
			report_ie(abstract_ie, LOG_WARNING, "IE oct 7 ext != 0\n");
			return FALSE;
		}

		ie->user_information_layer_3_protocol =
			oct_7->user_information_layer_3_protocol;
	}

	return TRUE;
}

int q931_ie_bearer_capability_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_bearer_capability *ie =
		container_of(abstract_ie, struct q931_ie_bearer_capability, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_BEARER_CAPABILITY;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_3 *oct_3 =
	  (struct q931_ie_bearer_capability_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->information_transfer_capability =
		ie->information_transfer_capability;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_4 *oct_4 =
	  (struct q931_ie_bearer_capability_onwire_4 *)(&ieow->data[ieow->len]);
	oct_4->ext = 1;
	oct_4->transfer_mode = ie->transfer_mode;
	oct_4->information_transfer_rate = ie->information_transfer_rate;
	ieow->len += 1;

	if (!(ie->transfer_mode == Q931_IE_BC_TM_CIRCUIT &&
	     (ie->information_transfer_capability ==
			Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL ||
	      ie->information_transfer_capability ==
			Q931_IE_BC_ITC_RESTRICTED_DIGITAL) &&
	      ie->user_information_layer_1_protocol ==
			Q931_IE_BC_UIL1P_UNUSED)) {

		ieow->data[ieow->len] = 0x00;
		struct q931_ie_bearer_capability_onwire_5 *oct_5 =
		  (struct q931_ie_bearer_capability_onwire_5 *)(&ieow->data[ieow->len]);
		oct_5->ext = 1;
		oct_5->layer_1_ident = Q931_IE_BC_LAYER_1_IDENT;
		oct_5->user_information_layer_1_protocol =
			ie->user_information_layer_1_protocol;
		ieow->len += 1;
	}

	if (ie->transfer_mode == Q931_IE_BC_TM_PACKET ||
	    ie->user_information_layer_2_protocol !=
			Q931_IE_BC_UIL2P_UNUSED) {
		// oct_6
		assert(0);
	}

	if (ie->user_information_layer_2_protocol !=
			Q931_IE_BC_UIL3P_UNUSED) {
		// oct_7
		assert(0);
	}

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_bearer_capability_coding_standard_to_text(
	enum q931_ie_bearer_capability_coding_standard coding_standard)
{
	switch(coding_standard) {
	case Q931_IE_BC_CS_CCITT:
		return "CCITT";
	case Q931_IE_BC_CS_RESERVED:
		return "Reserved";
	case Q931_IE_BC_CS_NATIONAL:
		return "National";
	case Q931_IE_BC_CS_SPECIFIC:
		return "Specific";
	default:
		return "*INVALID*";
	}
}

static const char *
	q931_ie_bearer_capability_information_transfer_capability_to_text(
	enum q931_ie_bearer_capability_information_transfer_capability
		information_transfer_capability)
{
	switch(information_transfer_capability) {
	case Q931_IE_BC_ITC_SPEECH:
		return "Speech";
	case Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL:
		return "Unrestricted Digital";
	case Q931_IE_BC_ITC_RESTRICTED_DIGITAL:
		return "Restricted Digital";
	case Q931_IE_BC_ITC_3_1_KHZ_AUDIO:
		return "3.1 kHz audio";
	case Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL_WITH_TONES:
		return "Unrestricted digital with tones";
	case Q931_IE_BC_ITC_VIDEO:
		return "Video";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_bearer_capability_transfer_mode_to_text(
	enum q931_ie_bearer_capability_transfer_mode transfer_mode)
{
	switch(transfer_mode) {
	case Q931_IE_BC_TM_CIRCUIT:
		return "Circuit";
	case Q931_IE_BC_TM_PACKET:
		return "Packet";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_bearer_capability_information_transfer_rate_to_text(
	enum q931_ie_bearer_capability_information_transfer_rate
		information_transfer_rate)
{
	switch(information_transfer_rate) {
	case Q931_IE_BC_ITR_PACKET:
		return "Packet";
	case Q931_IE_BC_ITR_64:
		return "64 kbps";
	case Q931_IE_BC_ITR_64_X_2:
		return "2 x 64 kbps";
	case Q931_IE_BC_ITR_384:
		return "384 kbps";
	case Q931_IE_BC_ITR_1536:
		return "1536 kbps";
	case Q931_IE_BC_ITR_1920:
		return "1920 kbps";
	default:
		return "*INVALID*";
	}
}

static const char *
	q931_ie_bearer_capability_user_information_layer_1_protocol_to_text(
	enum q931_ie_bearer_capability_user_information_layer_1_protocol
		user_information_layer_1_protocol)
{
	switch(user_information_layer_1_protocol) {
	case Q931_IE_BC_UIL1P_V110:
		return "v.110";
	case Q931_IE_BC_UIL1P_G711_ULAW:
		return "g.711 u-law";
	case Q931_IE_BC_UIL1P_G711_ALAW:
		return "g.711 a-law";
	case Q931_IE_BC_UIL1P_G721:
		return "g.721";
	case Q931_IE_BC_UIL1P_G722:
		return "g.722";
	case Q931_IE_BC_UIL1P_G7XX_VIDEO:
		return "g.7xx video";
	case Q931_IE_BC_UIL1P_NON_CCITT:
		return "Non-CCITT";
	case Q931_IE_BC_UIL1P_V120:
		return "v.120";
	case Q931_IE_BC_UIL1P_X31:
		return "x.31";
	case Q931_IE_BC_UIL1P_UNUSED:
		return "-----";
	default:
		return "*INVALID*";
	}
}

void q931_ie_bearer_capability_dump(
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_bearer_capability *ie =
		container_of(abstract_ie, struct q931_ie_bearer_capability, ie);

	report_ie_dump(abstract_ie,
		"%sCoding Standard = %s (%d)\n", prefix,
		q931_ie_bearer_capability_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	report_ie_dump(abstract_ie,
		"%sInformation Transfer Capability = %s (%d)\n", prefix,
		q931_ie_bearer_capability_information_transfer_capability_to_text(
			ie->information_transfer_capability),
		ie->information_transfer_capability);

	report_ie_dump(abstract_ie,
		"%sTransfer mode = %s (%d)\n", prefix,
		q931_ie_bearer_capability_transfer_mode_to_text(
			ie->transfer_mode),
		ie->transfer_mode);

	report_ie_dump(abstract_ie,
		"%sInformation Transfer Rate = %s (%d)\n", prefix,
		q931_ie_bearer_capability_information_transfer_rate_to_text(
			ie->information_transfer_rate),
		ie->information_transfer_rate);

	report_ie_dump(abstract_ie, 
		"%sUser information layer 1 protocol = %s (%d)\n", prefix,
		q931_ie_bearer_capability_user_information_layer_1_protocol_to_text(
			ie->user_information_layer_1_protocol),
		ie->user_information_layer_1_protocol);
}

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

#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/ie_cgpn.h>

static const struct q931_ie_type *ie_type;

void q931_ie_calling_party_number_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_calling_party_number *q931_ie_calling_party_number_alloc()
{
	struct q931_ie_calling_party_number *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	memset(ie->number, 0x0, sizeof(*ie->number));

	return ie;
}

struct q931_ie *q931_ie_calling_party_number_alloc_abstract()
{
	return &q931_ie_calling_party_number_alloc()->ie;
}

int q931_ie_calling_party_number_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_calling_party_number *ie =
		container_of(abstract_ie,
			struct q931_ie_calling_party_number, ie);

	int nextoct = 0;

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_calling_party_number_onwire_3 *oct_3 =
		(struct q931_ie_calling_party_number_onwire_3 *)
		(msg->rawies + pos + (nextoct++));

	ie->type_of_number = oct_3->type_of_number;
	ie->numbering_plan_identificator = oct_3->numbering_plan_identificator;

	if (oct_3->ext) {
		struct q931_ie_calling_party_number_onwire_3a *oct_3a =
			(struct q931_ie_calling_party_number_onwire_3a *)
			(msg->rawies + pos + (nextoct++));

		ie->presentation_indicator = oct_3a->presentation_indicator;
		ie->screening_indicator = oct_3a->screening_indicator;
	} else {
		ie->presentation_indicator =
			Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		ie->screening_indicator =
			Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED;
	}

	memcpy(ie->number, msg->rawies + pos + nextoct, len - nextoct);
	ie->number[len] = '\0';

	return TRUE;
}

int q931_ie_calling_party_number_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_calling_party_number *ie =
		container_of(generic_ie, struct q931_ie_calling_party_number, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	// Check max_size

	ieow->id = Q931_IE_CALLING_PARTY_NUMBER;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_calling_party_number_onwire_3 *oct_3 =
	  (struct q931_ie_calling_party_number_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 0;
	oct_3->type_of_number = ie->type_of_number;
	oct_3->numbering_plan_identificator = ie->numbering_plan_identificator;
	ieow->len++;

	struct q931_ie_calling_party_number_onwire_3a *oct_3a =
	  (struct q931_ie_calling_party_number_onwire_3a *)(&ieow->data[ieow->len]);
	oct_3a->ext = 1;
	oct_3a->presentation_indicator = ie->presentation_indicator;
	oct_3a->screening_indicator = ie->screening_indicator;
	ieow->len++;

	memcpy(&ieow->data[ieow->len], ie->number, strlen(ie->number));
	ieow->len += strlen(ie->number);

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_calling_party_number_type_of_number_to_text(
        enum q931_ie_calling_party_number_type_of_number
                type_of_number)
{
        switch(type_of_number) {
	case Q931_IE_CGPN_TON_UNKNOWN:
		return "Unknown";
	case Q931_IE_CGPN_TON_INTERNATIONAL:
		return "International";
	case Q931_IE_CGPN_TON_NATIONAL:
		return "National";
	case Q931_IE_CGPN_TON_NETWORK_SPECIFIC:
		return "Network specific";
	case Q931_IE_CGPN_TON_SUBSCRIBER:
		return "Subscriber";
	case Q931_IE_CGPN_TON_ABBREVIATED:
		return "Abbreviated";
	case Q931_IE_CGPN_TON_RESERVED_FOR_EXT:
		return "Reserved";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_calling_party_number_numbering_plan_identificator_to_text(
        enum q931_ie_calling_party_number_numbering_plan_identificator
                numbering_plan_identificator)
{
        switch(numbering_plan_identificator) {
	case Q931_IE_CGPN_NPI_UNKNOWN:
		return "Unknown";
	case Q931_IE_CGPN_NPI_ISDN_TELEPHONY:
		return "ISDN Telephony";
	case Q931_IE_CGPN_NPI_DATA:
		return "Data";
	case Q931_IE_CGPN_NPI_TELEX:
		return "Telex";
	case Q931_IE_CGPN_NPI_NATIONAL_STANDARD:
		return "National standard";
	case Q931_IE_CGPN_NPI_PRIVATE:
		return "Private";
	case Q931_IE_CGPN_NPI_RESERVED_FOR_EXT:
		return "Reserved";
	default:
		return "*INVALID*";
	}
}


void q931_ie_calling_party_number_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_calling_party_number *ie =
		container_of(generic_ie, struct q931_ie_calling_party_number, ie);

	report(LOG_DEBUG, "%sType of number = %s (%d)\n", prefix,
		q931_ie_calling_party_number_type_of_number_to_text(
			ie->type_of_number),
		ie->type_of_number);

	report(LOG_DEBUG, "%sNumbering plan = %s (%d)\n", prefix,
		q931_ie_calling_party_number_numbering_plan_identificator_to_text(
			ie->numbering_plan_identificator),
		ie->numbering_plan_identificator);

	report(LOG_DEBUG, "%sNumber = %s\n", prefix,
		ie->number);
}

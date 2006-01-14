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
#include <libq931/ie_called_party_number.h>

static const struct q931_ie_class *my_class;

void q931_ie_called_party_number_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_called_party_number *q931_ie_called_party_number_alloc()
{
	struct q931_ie_called_party_number *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.cls = my_class;

	memset(ie->number, 0x0, sizeof(*ie->number));

	return ie;
}

struct q931_ie *q931_ie_called_party_number_alloc_abstract()
{
	return &q931_ie_called_party_number_alloc()->ie;
}

int q931_ie_called_party_number_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	struct q931_ie_called_party_number *ie =
		container_of(abstract_ie,
			struct q931_ie_called_party_number, ie);

	if (len < 1) {
		report_ie(abstract_ie, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	int nextoct = 0;

	struct q931_ie_called_party_number_onwire_3 *oct_3 =
		(struct q931_ie_called_party_number_onwire_3 *)
		(buf + nextoct++);

	ie->type_of_number = oct_3->type_of_number;
	ie->numbering_plan_identificator = oct_3->numbering_plan_identificator;

	memcpy(ie->number, buf + 1, len - 1);
	ie->number[len] = '\0';

	return TRUE;
}

int q931_ie_called_party_number_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	int len = 0;
	struct q931_ie_called_party_number *ie =
		container_of(abstract_ie,
			struct q931_ie_called_party_number, ie);

	// Check max_size

	struct q931_ie_called_party_number_onwire_3 *oct_3 =
		(struct q931_ie_called_party_number_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
	oct_3->ext = 1;
	oct_3->type_of_number = ie->type_of_number;
	oct_3->numbering_plan_identificator = ie->numbering_plan_identificator;
	len++;

	memcpy(buf + len, ie->number, strlen(ie->number));
	len += strlen(ie->number);

	return len;
}

static const char *q931_ie_called_party_number_type_of_number_to_text(
	enum q931_ie_called_party_number_type_of_number type_of_number)
{
	switch(type_of_number) {
	case Q931_IE_CDPN_TON_UNKNOWN:
		return "Unknown";
	case Q931_IE_CDPN_TON_INTERNATIONAL:
		return "International";
	case Q931_IE_CDPN_TON_NATIONAL:
		return "National";
	case Q931_IE_CDPN_TON_NETWORK_SPECIFIC:
		return "Network specific";
	case Q931_IE_CDPN_TON_SUBSCRIBER:
		return "Subscriber";
	case Q931_IE_CDPN_TON_ABBREVIATED:
		return "Abbreviated";
	case Q931_IE_CDPN_TON_RESERVED_FOR_EXT:
		return "Reserved";
	default:
		return "*INVALID*";
	}
}

static const char *
	q931_ie_called_party_number_numbering_plan_identificator_to_text(
		enum q931_ie_called_party_number_numbering_plan_identificator
			numbering_plan_identificator)
{
	switch(numbering_plan_identificator) {
	case Q931_IE_CDPN_NPI_UNKNOWN:
		return "Unknown";
	case Q931_IE_CDPN_NPI_ISDN_TELEPHONY:
		return "ISDN Telephony";
	case Q931_IE_CDPN_NPI_DATA:
		return "Data";
	case Q931_IE_CDPN_NPI_TELEX:
		return "Telex";
	case Q931_IE_CDPN_NPI_NATIONAL_STANDARD:
		return "National standard";
	case Q931_IE_CDPN_NPI_PRIVATE:
		return "Private";
	case Q931_IE_CDPN_NPI_RESERVED_FOR_EXT:
		return "Reserved";
	default:
		return "*INVALID*";
	}
}


void q931_ie_called_party_number_dump(
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_called_party_number *ie =
		container_of(abstract_ie, struct q931_ie_called_party_number, ie);

	report_ie_dump(abstract_ie,  "%sType of number = %s (%d)\n", prefix,
		q931_ie_called_party_number_type_of_number_to_text(
			ie->type_of_number),
		ie->type_of_number);

	report_ie_dump(abstract_ie, "%sNumbering plan = %s (%d)\n", prefix,
		q931_ie_called_party_number_numbering_plan_identificator_to_text(
			ie->numbering_plan_identificator),
		ie->numbering_plan_identificator);

	report_ie_dump(abstract_ie, "%sNumber = %s\n", prefix,
		ie->number);
}


#include <string.h>

#define Q931_PRIVATE

#include "ie_cgpn.h"

static const struct q931_ie_type *ie_type;

void q931_ie_calling_party_number_init(
	struct q931_ie_calling_party_number *ie)
{
	ie->ie.type = ie_type;
}

void q931_ie_calling_party_number_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

int q931_ie_calling_party_number_check(
	const struct q931_ie *ie,
	const struct q931_message *msg)
{
	// TODO

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
	struct q931_ie_calling_party_number_onwire_3_4 *oct_3 =
	  (struct q931_ie_calling_party_number_onwire_3_4 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->type_of_number = ie->type_of_number;
	oct_3->numbering_plan_identificator = ie->numbering_plan_identificator;
	ieow->len += 1;

	memcpy(&ieow->data[ieow->len], ie->number, strlen(ie->number));
	ieow->len += strlen(ie->number);

	return ieow->len + sizeof(struct q931_ie_onwire);
}

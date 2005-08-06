#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ie_cdpn.h"

static const struct q931_ie_type *ie_type;

void q931_ie_called_party_number_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_called_party_number *q931_ie_called_party_number_alloc()
{
	struct q931_ie_called_party_number *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	memset(ie->number, 0x0, sizeof(*ie->number));

	return ie;
}

struct q931_ie *q931_ie_called_party_number_alloc_abstract()
{
	return &q931_ie_called_party_number_alloc()->ie;
}

int q931_ie_called_party_number_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_called_party_number *ie =
		container_of(abstract_ie,
			struct q931_ie_called_party_number, ie);

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_called_party_number_onwire_3 *oct_3 =
		(struct q931_ie_called_party_number_onwire_3 *)
		(msg->rawies + pos + 0);

	ie->type_of_number = oct_3->type_of_number;
	ie->numbering_plan_identificator = oct_3->numbering_plan_identificator;

	memcpy(ie->number, msg->rawies + pos + 1, len - 1);
	ie->number[len] = '\0';

	return TRUE;
}

int q931_ie_called_party_number_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_called_party_number *ie =
		container_of(generic_ie, struct q931_ie_called_party_number, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	// Check max_size

	ieow->id = Q931_IE_CALLED_PARTY_NUMBER;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_called_party_number_onwire_3 *oct_3 =
	  (struct q931_ie_called_party_number_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->type_of_number = ie->type_of_number;
	oct_3->numbering_plan_identificator = ie->numbering_plan_identificator;
	ieow->len += 1;

	memcpy(&ieow->data[ieow->len], ie->number, strlen(ie->number));
	ieow->len += strlen(ie->number);

	return ieow->len + sizeof(struct q931_ie_onwire);
}

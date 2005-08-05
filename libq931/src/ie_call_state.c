#include <string.h>
#include <stdlib.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "ie_call_state.h"

static const struct q931_ie_type *ie_type;

void q931_ie_call_state_init(
	struct q931_ie_call_state *ie)
{
	ie->ie.type = ie_type;
}

void q931_ie_call_state_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

int q931_ie_call_state_check(
	const struct q931_ie *ie,
	const struct q931_message *msg)
{
	if (ie->len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");

		return FALSE;
	}

	struct q931_ie_call_state_onwire_3 *oct_3 =
		(struct q931_ie_call_state_onwire_3 *)
		(ie->data + 0);

	if (oct_3->coding_standard != Q931_IE_CS_CS_CCITT) {
		report_msg(msg, LOG_ERR, "coding stanrdard != CCITT\n");

		return FALSE;
	}

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


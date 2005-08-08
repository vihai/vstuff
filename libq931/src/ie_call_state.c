#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "ie_call_state.h"

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


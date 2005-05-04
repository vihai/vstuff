#include <string.h>
#include <stdlib.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "ie_call_state.h"

int q931_ie_call_state_check(
	const struct q931_message *msg,
	const struct q931_ie *ie)
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

int q931_append_ie_call_state(void *buf, __u8 value)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CALL_STATE;
	ie->len = 0;

	ie->data[ie->len] = 0x00;
	struct q931_ie_call_state_onwire_3 *oct_3 =
	  (struct q931_ie_call_state_onwire_3 *)(&ie->data[ie->len]);
	oct_3->coding_standard = Q931_IE_CS_CS_CCITT;
	oct_3->value = value;
	ie->len += 1;

	return ie->len + sizeof(struct q931_ie_onwire);
}


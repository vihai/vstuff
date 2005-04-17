#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "intf.h"
#include "message.h"

#include "ie_restind.h"

int q931_ie_restart_indicator_check(
	struct q931_message *msg,
	struct q931_ie *ie)
{
	if (ie->len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");

		return FALSE;
	}

	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
		(struct q931_ie_restart_indicator_onwire_3 *)
		(ie->data + 0);

	if (oct_3->ext != 1) {
		report_msg(msg, LOG_ERR,
			"Ext != 1\n");

		return FALSE;
	}

	if (oct_3->restart_class != Q931_IE_RI_C_INDICATED &&
	    oct_3->restart_class != Q931_IE_RI_C_SIGNLE_INTERFACE &&
	    oct_3->restart_class != Q931_IE_RI_C_ALL_INTERFACES) {
		report_msg(msg, LOG_ERR,
			"IE specifies invalid class\n");

		return FALSE;
	}

	return TRUE;
}

int q931_append_ie_restart_indicator(void *buf,
	enum q931_ie_restart_indicator_class restart_class)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_RESTART_INDICATOR;
	ie->len = 0;

	ie->data[ie->len] = 0x00;
	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
	  (struct q931_ie_restart_indicator_onwire_3 *)(&ie->data[ie->len]);
	oct_3->ext = 1;
	oct_3->restart_class = restart_class;
	ie->len += 1;

	return ie->len + sizeof(struct q931_ie_onwire);
}

#include <string.h>

#define Q931_PRIVATE

#include "call.h"
#include "logging.h"
#include "ie_chanid.h"
#include "intf.h"

static inline int q931_ie_channel_identification_check_bra(
	struct q931_call *call,
	struct q931_ie *ie)
{
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(ie->data + 0);

	if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {

		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_call(call, LOG_ERR,
				"IE specifies any channel with no alternative"
				" is this valid?\n");

			return FALSE;
		}
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_NO_CHANNEL) {
		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_call(call, LOG_ERR,
				"IE specifies no channel with no alternative"
				" is this valid?\n");

			return FALSE;
		}
	}

	return TRUE;
}

static inline int q931_ie_channel_identification_check_pra(
	struct q931_call *call,
	struct q931_ie *ie)
{
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		(ie->data + 1);

	if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
		report_call(call, LOG_ERR,
			"IE specifies unsupported coding type\n");

		return FALSE;
	}

	struct q931_ie_channel_identification_onwire_3d *oct_3d;
	do {
		oct_3d = (struct q931_ie_channel_identification_onwire_3d *)
				(ie->data + 2);

		// FIXME

	} while (!oct_3d->ext);

	return TRUE;
}

int q931_ie_channel_identification_check(
	struct q931_call *call,
	struct q931_ie *ie)
{
	if (ie->size < 2) {
		report_call(call, LOG_ERR, "IE size < 2\n");

		return FALSE;
	}

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(ie->data + 0);

	if (oct_3->interface_id_present == Q931_IE_CI_IIP_EXPLICIT) {
		report_call(call, LOG_ERR,
			"IE specifies interface ID thought it"
			" is not supported by the ETSI\n");

		return FALSE;
	}

	if ((oct_3->interface_type == Q931_IE_CI_IT_BASIC &&
	     call->intf->type == Q931_INTF_TYPE_PRA) ||
	   ((oct_3->interface_type == Q931_IE_CI_IT_PRIMARY &&
	    (call->intf->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	     call->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT)))) {
		report_call(call, LOG_ERR,
			"IE specifies wrong interface type\n");

		return FALSE;
	}

	if (oct_3->d_channel_indicator == Q931_IE_CI_DCI_IS_D_CHAN) {
		report_call(call, LOG_ERR,
			"IE specifies D channel which"
			" is not supported\n");

		return FALSE;
	}

	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		(ie->data + 1);
	if (oct_3c->number_map == Q931_IE_CI_NM_MAP) {
		report_call(call, LOG_ERR,
			"IE specifies channel map, which"
			" is not supported by DSSS-1\n");

		return FALSE;
	}

	if (oct_3->interface_type == Q931_IE_CI_IT_BASIC)
		return q931_ie_channel_identification_check_bra(call, ie);
	else
		return q931_ie_channel_identification_check_pra(call, ie);
}

int q931_append_ie_channel_identification_bra(void *buf,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	enum q931_ie_channel_identification_info_channel_selection_bra chan_id)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ie->size = 0;

	ie->data[ie->size] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ie->data[ie->size]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_BASIC;
	oct_3->preferred_exclusive = prefexcl;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = chan_id;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}

int q931_append_ie_channel_identification_pra(void *buf,
	enum q931_ie_channel_identification_info_channel_selection_pra selection,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	int chan_id)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ie->size = 0;

	ie->data[ie->size] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ie->data[ie->size]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_PRIMARY;
	oct_3->preferred_exclusive = prefexcl;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = selection;
	ie->size += 1;

	// Interface implicit, do not add Interface identifier

	ie->data[ie->size] = 0x00;
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
	  (struct q931_ie_channel_identification_onwire_3c *)(&ie->data[ie->size]);
	oct_3c->ext = 1;
	oct_3c->coding_standard = Q931_IE_CI_CS_CCITT;
	oct_3c->number_map = Q931_IE_CI_NM_NUMBER;
	oct_3c->channel_type_map_identifier_type = Q931_IE_CI_ET_B;
	ie->size += 1;

	ie->data[ie->size] = 0x00;
	struct q931_ie_channel_identification_onwire_3d *oct_3d =
	  (struct q931_ie_channel_identification_onwire_3d *)(&ie->data[ie->size]);
	oct_3d->ext = 1;
	oct_3d->channel_number = chan_id;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}

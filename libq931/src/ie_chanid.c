#include <string.h>

#include "ie_chanid.h"

static inline q931_ie_channel_identification_check_bra(struct q931_ies *ies)
{
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		ies[i]->data[0];

	if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {

		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_call(call, LOG_ERR,
				"IE specifies any channel with no alternative"
				" is this valid?\n");

			goto invalid_ie;
		}
	else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_NO_CHANNEL) {
		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_call(call, LOG_ERR,
				"IE specifies no channel with no alternative"
				" is this valid?\n");

			goto invalid_ie;
		}
	}
}

static inline q931_ie_channel_identification_check_pra(struct q931_ies *ies)
{
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		ies[i]->data[1];

	if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
		report_call(call, LOG_ERR,
			"IE specifies unsupported coding type\n");

		goto invalid_ie;
	}

	struct q931_ie_channel_identification_onwire_3d *oct_3d;
	do {
		oct_3d = (struct q931_ie_channel_identification_onwire_3d *)
				ies[i]->data[2];

		// FIXME

	} while (!oct_3d->ext);
}

int q931_ie_channel_identification_check(
	struct q931_call *call,
	struct q931_ies *ies)
{
	if (ies[i].size < 2) {
		report_call(call, LOG_ERR, "IE size < 2\n");

		goto invalid_ie;
	}

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		ies[i]->data[0];

	if (oct_3->interface_id_present == Q931_IE_CI_IIP_EXPLICIT) {
		report_call(call, LOG_ERR,
			"IE specifies interface ID thought it"
			" is not supported by the ETSI\n");

		goto invalid_ie;
	}

	if ((oct_3->interface_type == Q931_IE_CI_IT_BASIC &&
	     call->interface->type == Q931_INTF_TYPE_PRA) ||
	   ((oct_3->interface_type == Q931_IE_CI_IT_PRIMARY &&
	    (call->interface->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	     call->interface->type == Q931_INTF_TYPE_BRA_MULTIPOINT)))) {
		report_call(call, LOG_ERR,
			"IE specifies wrong interface type\n");

		goto invalid_ie;
	}

	if (oct_3->d_channel_indicator == Q931_IE_CI_DCI_IS_D_CHAN) {
		report_call(call, LOG_ERR,
			"IE specifies D channel which"
			" is not supported\n");

		goto invalid_ie;
	}

	if (oct_3c->number_map == Q931_IE_CI_NM_MAP) {
		report_call(call, LOG_ERR,
			"IE specifies channel map, which"
			" is not supported by DSSS-1\n");

		goto invalid_ie;
	}

	if (oct_3->interface_type == Q931_IE_CI_IT_BASIC) {
		return q931_ie_channel_identification_check_bra(ies);
	} else {
	}


invalid_ie:
	q931_send_status(call, call->dlc,
		Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

}

static struct q931_channel *q931_select_channel(
	struct q931_call *call,
	const struct q931_ies *ies)
{
}

int q931_append_ie_channel_identification_bra(void *buf,
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
	oct_3->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = chan_id;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}

int q931_append_ie_channel_identification_pra(void *buf,
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
	oct_3->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = Q931_IE_CI_ICS_PRA_INDICATED;
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

int q931_append_ie_channel_identification_any_bra(void *buf)
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
	oct_3->preferred_exclusive = Q931_IE_CI_PE_PREFERRED;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_ANY;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}

int q931_append_ie_channel_identification_any_pra(void *buf)
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
	oct_3->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = Q931_IE_CI_ICS_PRA_ANY;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}


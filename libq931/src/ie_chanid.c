#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "ie_chanid.h"
#include "intf.h"
#include "message.h"
#include "chanset.h"

static inline int q931_ie_channel_identification_check_bra(
	struct q931_message *msg,
	struct q931_ie *ie)
{
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(ie->data + 0);

	if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2) {
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {

		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_msg(msg, LOG_ERR,
				"IE specifies any channel with no alternative"
				" is this valid?\n");

			return FALSE;
		}
	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_NO_CHANNEL) {
		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			report_msg(msg, LOG_ERR,
				"IE specifies no channel with no alternative"
				" is this valid?\n");

			return FALSE;
		}
	}

	return TRUE;
}

static inline int q931_ie_channel_identification_check_pra(
	struct q931_message *msg,
	struct q931_ie *ie)
{
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		(ie->data + 1);

	if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
		report_msg(msg, LOG_ERR,
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
	struct q931_message *msg,
	struct q931_ie *ie)
{
	if (ie->len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 2\n");

		return FALSE;
	}

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(ie->data + 0);

	if (oct_3->interface_id_present == Q931_IE_CI_IIP_EXPLICIT) {
		report_msg(msg, LOG_ERR,
			"IE specifies interface ID thought it"
			" is not supported by the ETSI\n");

		return FALSE;
	}

	if (oct_3->d_channel_indicator == Q931_IE_CI_DCI_IS_D_CHAN) {
		report_msg(msg, LOG_ERR,
			"IE specifies D channel which"
			" is not supported\n");

		return FALSE;
	}

	if (msg->dlc->intf->type == Q931_INTF_TYPE_PRA) {
		if (ie->len < 2) {
			report_msg(msg, LOG_ERR, "IE size < 2\n");

			return FALSE;
		}

		if (oct_3->interface_type == Q931_IE_CI_IT_BASIC) {
			report_msg(msg, LOG_ERR,
				"IE specifies BRI while interface is PRI\n");

			return FALSE;
		}

		struct q931_ie_channel_identification_onwire_3c *oct_3c =
			(struct q931_ie_channel_identification_onwire_3c *)
			(ie->data + 1);
		if (oct_3c->number_map == Q931_IE_CI_NM_MAP) {
			report_msg(msg, LOG_ERR,
				"IE specifies channel map, which"
				" is not supported by DSSS-1\n");

			return FALSE;
		}

		return q931_ie_channel_identification_check_pra(msg, ie);

	} else {
		if (oct_3->interface_type == Q931_IE_CI_IT_PRIMARY) {
			report_msg(msg, LOG_ERR,
				"IE specifies BRI while interface is PRI\n");

			return FALSE;
		}

		return q931_ie_channel_identification_check_bra(msg, ie);
	}
}

void q931_ie_channel_identification_to_chanset(
	struct q931_ie *ie,
	struct q931_chanset *chanset)
{
	assert(chanset);
	assert(ie);
	assert(ie->info);
	assert(ie->info->id == Q931_IE_CHANNEL_IDENTIFICATION);

	chanset->nchans = 0;

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		ie->data;

	if (oct_3->interface_type == Q931_IE_CI_IT_BASIC) {
		if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1 ||
		    oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {
			chanset->chans[chanset->nchans] = 0;
			chanset->nchans++;
		} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2 ||
		           oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {
			chanset->chans[chanset->nchans] = 1;
			chanset->nchans++;
		}
	} else {
		// TODO
	}
}

int q931_append_ie_channel_identification_bra(void *buf,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	struct q931_chanset *chanset)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ie->len = 0;

	ie->data[ie->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ie->data[ie->len]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_BASIC;
	oct_3->preferred_exclusive = prefexcl;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;

	if (!chanset || chanset->nchans == 0) {
		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_NO_CHANNEL;
	} else if (chanset->nchans == 1) {
		if (chanset->chans[0] == 0)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B1;
		else if (chanset->chans[0] == 1)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B2;
		else
			assert(0);
	} else if (chanset->nchans == 2) {
		assert(chanset->chans[0] == 0 || chanset->chans[0] == 1);
		assert(chanset->chans[1] == 0 || chanset->chans[1] == 1);

		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_ANY;
	} else {
		assert(0);
	}

	ie->len += 1;

	return ie->len + sizeof(struct q931_ie_onwire);
}

int q931_append_ie_channel_identification_pra(void *buf,
	enum q931_ie_channel_identification_info_channel_selection_pra selection,
	enum q931_ie_channel_identification_preferred_exclusive prefexcl,
	struct q931_chanset *chanset)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ie->len = 0;

	ie->data[ie->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ie->data[ie->len]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_PRIMARY;
	oct_3->preferred_exclusive = prefexcl;
	oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
	oct_3->info_channel_selection = selection;
	ie->len += 1;

	// Interface implicit, do not add Interface identifier

	ie->data[ie->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
	  (struct q931_ie_channel_identification_onwire_3c *)(&ie->data[ie->len]);
	oct_3c->ext = 1;
	oct_3c->coding_standard = Q931_IE_CI_CS_CCITT;
	oct_3c->number_map = Q931_IE_CI_NM_NUMBER;
	oct_3c->channel_type_map_identifier_type = Q931_IE_CI_ET_B;
	ie->len += 1;

	ie->data[ie->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3d *oct_3d =
	  (struct q931_ie_channel_identification_onwire_3d *)(&ie->data[ie->len]);
	oct_3d->ext = 1;

	int i;
	for (i=0; i<chanset->nchans; i++) {
		oct_3d->channel_number = chanset->chans[i];
		ie->len += 1;
	}

	return ie->len + sizeof(struct q931_ie_onwire);
}

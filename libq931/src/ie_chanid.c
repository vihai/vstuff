#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "ie_chanid.h"
#include "intf.h"
#include "message.h"
#include "chanset.h"

static const struct q931_ie_type *ie_type;

void q931_ie_channel_identification_init(
	struct q931_ie_channel_identification *ie)
{
	ie->ie.type = ie_type;
}

void q931_ie_channel_identification_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

static inline int q931_ie_channel_identification_check_bra(
	const struct q931_ie *ie,
	const struct q931_message *msg)
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
	const struct q931_ie *ie,
	const struct q931_message *msg)
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
	const struct q931_ie *ie,
	const struct q931_message *msg)
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

		return q931_ie_channel_identification_check_pra(ie, msg);

	} else {
		if (oct_3->interface_type == Q931_IE_CI_IT_PRIMARY) {
			report_msg(msg, LOG_ERR,
				"IE specifies BRI while interface is PRI\n");

			return FALSE;
		}

		return q931_ie_channel_identification_check_bra(ie, msg);
	}
}

void q931_ie_channel_identification_to_chanset(
	const struct q931_ie *ie,
	struct q931_chanset *chanset)
{
	assert(chanset);
	assert(ie);
	assert(ie->type);
	assert(ie->type->id == Q931_IE_CHANNEL_IDENTIFICATION);

	chanset->nchans = 0;

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		ie->data;

	if (oct_3->interface_type == Q931_IE_CI_IT_BASIC) {
		if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1 ||
		    oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {
			chanset->chans[chanset->nchans] = 0;
			chanset->nchans++;
		}

		if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2 ||
		    oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {
			chanset->chans[chanset->nchans] = 1;
			chanset->nchans++;
		}
	} else {
		// TODO
	}
}

int q931_ie_channel_identification_write_to_buf_bra(
	const struct q931_ie_channel_identification *ie,
	void *buf,
	int max_size)
{
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_BASIC;
	oct_3->preferred_exclusive = ie->preferred_exclusive;
	oct_3->d_channel_indicator = ie->d_channel_indicator;

	if (ie->chanset.nchans == 0) {
		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_NO_CHANNEL;
	} else if (ie->chanset.nchans == 1) {
		if (ie->chanset.chans[0] == 0)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B1;
		else if (ie->chanset.chans[0] == 1)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B2;
		else
			assert(0);
	} else if (ie->chanset.nchans == 2) {
		assert(ie->chanset.chans[0] == 0 || ie->chanset.chans[0] == 1);
		assert(ie->chanset.chans[1] == 0 || ie->chanset.chans[1] == 1);

		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_ANY;
	} else {
		assert(0);
	}

	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static int q931_ie_channel_identification_write_to_buf_pra(
	const struct q931_ie_channel_identification *ie,
	void *buf,
	int max_size)
{
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
	  (struct q931_ie_channel_identification_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_PRIMARY;
	oct_3->preferred_exclusive = ie->preferred_exclusive;
	oct_3->d_channel_indicator = ie->d_channel_indicator;
	oct_3->info_channel_selection = Q931_IE_CI_ICS_PRA_NO_CHANNEL; //FIXME
	ieow->len += 1;

	// Interface implicit, do not add Interface identifier

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
	  (struct q931_ie_channel_identification_onwire_3c *)(&ieow->data[ieow->len]);
	oct_3c->ext = 1;
	oct_3c->coding_standard = Q931_IE_CI_CS_CCITT;
	oct_3c->number_map = Q931_IE_CI_NM_NUMBER;
	oct_3c->channel_type_map_identifier_type = Q931_IE_CI_ET_B;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3d *oct_3d =
	  (struct q931_ie_channel_identification_onwire_3d *)(&ieow->data[ieow->len]);
	oct_3d->ext = 1;

	int i;
	for (i=0; i<ie->chanset.nchans; i++) {
		oct_3d->channel_number = ie->chanset.chans[i];
		ieow->len += 1;
	}

	return ieow->len + sizeof(struct q931_ie_onwire);
}

int q931_ie_channel_identification_write_to_buf(
	const struct q931_ie *generic_ie,
        void *buf,
	int max_size)
{
	const struct q931_ie_channel_identification *ie =
		container_of(generic_ie, struct q931_ie_channel_identification, ie);

	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ieow->len = 0;

	if (ie->interface_type == Q931_IE_CI_IT_BASIC) {
		return q931_ie_channel_identification_write_to_buf_bra(
				ie, buf + sizeof(*ieow),
				max_size - sizeof(*ieow));
	} else {
		return q931_ie_channel_identification_write_to_buf_pra(
				ie, buf + sizeof(*ieow),
				max_size - sizeof(*ieow));
	}
}


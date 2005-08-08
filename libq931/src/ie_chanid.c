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

void q931_ie_channel_identification_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_channel_identification *q931_ie_channel_identification_alloc(void)
{
	struct q931_ie_channel_identification *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.type = ie_type;
	ie->ie.refcnt = 1;

	q931_chanset_init(&ie->chanset);

	return ie;
}

struct q931_ie *q931_ie_channel_identification_alloc_abstract(void)
{
	return &q931_ie_channel_identification_alloc()->ie;
}

int q931_ie_channel_identification_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_channel_identification *ie = 
		container_of(abstract_ie,
			struct q931_ie_channel_identification, ie);

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(msg->rawies + pos + 0);

	if (oct_3->interface_id_present == Q931_IE_CI_IIP_EXPLICIT) {
		report_msg(msg, LOG_ERR,
			"IE specifies interface ID thought it"
			" is not supported by the ETSI\n");

		return FALSE;
	}

	ie->interface_id_present = oct_3->interface_id_present;

	if (oct_3->d_channel_indicator == Q931_IE_CI_DCI_IS_D_CHAN) {
		report_msg(msg, LOG_ERR,
			"IE specifies D channel which"
			" is not supported\n");

		return FALSE;
	}

	ie->d_channel_indicator = oct_3->d_channel_indicator;

	if (msg->dlc->intf->type == Q931_INTF_TYPE_PRA) {
		if (len < 2) {
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
			(msg->rawies + pos + 1);
		if (oct_3c->number_map == Q931_IE_CI_NM_MAP) {
			report_msg(msg, LOG_ERR,
				"IE specifies channel map, which"
				" is not supported by DSSS-1\n");

			return FALSE;
		}

		if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
			report_msg(msg, LOG_ERR,
				"IE specifies unsupported coding type\n");

			return FALSE;
		}

		struct q931_ie_channel_identification_onwire_3d *oct_3d;
		do {
			oct_3d = (struct q931_ie_channel_identification_onwire_3d *)
					(msg->rawies + 2);

			// FIXME

		} while (!oct_3d->ext);

		return TRUE;

	} else {
		if (oct_3->interface_type == Q931_IE_CI_IT_PRIMARY) {
			report_msg(msg, LOG_ERR,
				"IE specifies BRI while interface is PRI\n");

			return FALSE;
		}

		if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
			q931_chanset_add(&ie->chanset, &msg->dlc->intf->channels[0]);
		} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2) {
			q931_chanset_add(&ie->chanset, &msg->dlc->intf->channels[1]);
		} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {
			q931_chanset_add(&ie->chanset, &msg->dlc->intf->channels[0]);
			q931_chanset_add(&ie->chanset, &msg->dlc->intf->channels[1]);

			if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
				report_msg(msg, LOG_ERR,
					"IE specifies any channel with no alternative,"
					" is this valid?\n");

				return FALSE;
			}
		} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_NO_CHANNEL) {
			if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
				report_msg(msg, LOG_ERR,
					"IE specifies no channel with no alternative,"
					" is this valid?\n");

				return FALSE;
			}
		}

		return TRUE;
	}
}

int q931_ie_channel_identification_write_to_buf_bra(
	struct q931_ie_onwire *ieow,
	const struct q931_ie_channel_identification *ie,
	int max_size)
{
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
		if (ie->chanset.chans[0]->id == 0)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B1;
		else if (ie->chanset.chans[0]->id == 1)
			oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_B2;
		else
			assert(0);
	} else if (ie->chanset.nchans == 2) {
		assert(ie->chanset.chans[0]->id == 0 || ie->chanset.chans[0]->id == 1);
		assert(ie->chanset.chans[1]->id == 0 || ie->chanset.chans[1]->id == 1);

		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_ANY;
	} else {
		assert(0);
	}

	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static int q931_ie_channel_identification_write_to_buf_pra(
	struct q931_ie_onwire *ieow,
	const struct q931_ie_channel_identification *ie,
	int max_size)
{
	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(&ieow->data[ieow->len]);
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
		(struct q931_ie_channel_identification_onwire_3c *)
		(&ieow->data[ieow->len]);
	oct_3c->ext = 1;
	oct_3c->coding_standard = Q931_IE_CI_CS_CCITT;
	oct_3c->number_map = Q931_IE_CI_NM_NUMBER;
	oct_3c->channel_type_map_identifier_type = Q931_IE_CI_ET_B;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_channel_identification_onwire_3d *oct_3d =
		(struct q931_ie_channel_identification_onwire_3d *)
		(&ieow->data[ieow->len]);
	oct_3d->ext = 1;

	int i;
	for (i=0; i<ie->chanset.nchans; i++) {
		oct_3d->channel_number = ie->chanset.chans[i]->id;
		ieow->len += 1;
	}

	return ieow->len + sizeof(struct q931_ie_onwire);
}

int q931_ie_channel_identification_write_to_buf(
	const struct q931_ie *abstract_ie,
        void *buf,
	int max_size)
{
	assert(abstract_ie->type == ie_type);
	const struct q931_ie_channel_identification *ie =
		container_of(abstract_ie, struct q931_ie_channel_identification, ie);

	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_CHANNEL_IDENTIFICATION;
	ieow->len = 0;

	if (ie->interface_type == Q931_IE_CI_IT_BASIC) {
		return q931_ie_channel_identification_write_to_buf_bra(
				ieow, ie, max_size);
	} else {
		return q931_ie_channel_identification_write_to_buf_pra(
				ieow, ie, max_size);
	}
}


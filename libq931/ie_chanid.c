/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/ie_chanid.h>
#include <libq931/intf.h>
#include <libq931/message.h>
#include <libq931/chanset.h>

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

static const char *q931_ie_channel_identification_interface_id_present_to_text(
	enum q931_ie_channel_identification_interface_id_present
		interface_id_present)
{
        switch(interface_id_present) {
	case Q931_IE_CI_IIP_IMPLICIT:
		return "Implicit";
	case Q931_IE_CI_IIP_EXPLICIT:
		return "Explicit";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_channel_identification_interface_type_to_text(
	enum q931_ie_channel_identification_interface_type
		interface_type)
{
        switch(interface_type) {
	case Q931_IE_CI_IT_BASIC:
		return "Basic";
	case Q931_IE_CI_IT_PRIMARY:
		return "Primary";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_channel_identification_preferred_exclusive_to_text(
	enum q931_ie_channel_identification_preferred_exclusive
		preferred_exclusive)
{
        switch(preferred_exclusive) {
	case Q931_IE_CI_PE_PREFERRED:
		return "Preferred";
	case Q931_IE_CI_PE_EXCLUSIVE:
		return "Exclusive";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_channel_identification_d_channel_indicator_to_text(
	enum q931_ie_channel_identification_d_channel_indicator
		d_channel_indicator)
{
        switch(d_channel_indicator) {
	case Q931_IE_CI_DCI_IS_NOT_D_CHAN:
		return "Is not D channel";
	case Q931_IE_CI_DCI_IS_D_CHAN:
		return "Is D channel";
	default:
		return "*INVALID*";
	}
}

/*static const char *q931_ie_channel_identification_info_channel_selection_bra_to_text(
	enum q931_ie_channel_identification_info_channel_selection_bra
		info_channel_selection_bra)
{
        switch(info_channel_selection_bra) {
	case Q931_IE_CI_ICS_BRA_NO_CHANNEL:
		return "No channel";
	case Q931_IE_CI_ICS_BRA_B1:
		return "B1";
	case Q931_IE_CI_ICS_BRA_B2:
		return "B2";
	case Q931_IE_CI_ICS_BRA_ANY:
		return "Any channel";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_channel_identification_info_channel_selection_pra_to_text(
	enum q931_ie_channel_identification_info_channel_selection_pra
		info_channel_selection)
{
        switch(info_channel_selection) {
	case Q931_IE_CI_ICS_PRA_NO_CHANNEL:
		return "No channel";
	case Q931_IE_CI_ICS_PRA_INDICATED:
		return "Indicated";
	case Q931_IE_CI_ICS_PRA_RESERVED:
		return "Reserved";
	case Q931_IE_CI_ICS_PRA_ANY:
		return "Any channel";
	default:
		return "*INVALID*";
	}
}*/

static const char *q931_ie_channel_identification_coding_standard_to_text(
	enum q931_ie_channel_identification_coding_standard
		coding_standard)
{
        switch(coding_standard) {
	case Q931_IE_CI_CS_CCITT:
		return "CCITT";
	case Q931_IE_CI_CS_RESERVED:
		return "Reserved";
	case Q931_IE_CI_CS_NATIONAL:
		return "National";
	case Q931_IE_CI_CS_NETWORK:
		return "Network";
	default:
		return "*INVALID*";
	}
}

void q931_ie_channel_identification_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_channel_identification *ie =
		container_of(generic_ie, struct q931_ie_channel_identification, ie);

	report(LOG_DEBUG, "%sInterface id = %s (%d)\n", prefix,
		q931_ie_channel_identification_interface_id_present_to_text(
			ie->interface_id_present),
		ie->interface_id_present);

	report(LOG_DEBUG, "%sInterface type = %s (%d)\n", prefix,
		q931_ie_channel_identification_interface_type_to_text(
			ie->interface_type),
		ie->interface_type);

	report(LOG_DEBUG, "%sPref/Excl = %s (%d)\n", prefix,
		q931_ie_channel_identification_preferred_exclusive_to_text(
			ie->preferred_exclusive),
		ie->preferred_exclusive);

	report(LOG_DEBUG, "%sD channel ident = %s (%d)\n", prefix,
		q931_ie_channel_identification_d_channel_indicator_to_text(
			ie->d_channel_indicator),
		ie->d_channel_indicator);

	report(LOG_DEBUG, "%sCoding standard = %s (%d)\n", prefix,
		q931_ie_channel_identification_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	// FIXME Add chanset
}

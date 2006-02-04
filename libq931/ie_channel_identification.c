/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/ie_channel_identification.h>
#include <libq931/intf.h>
#include <libq931/message.h>
#include <libq931/chanset.h>

static const struct q931_ie_class *my_class;

void q931_ie_channel_identification_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_channel_identification *
	q931_ie_channel_identification_alloc(void)
{
	struct q931_ie_channel_identification *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.cls = my_class;
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
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);
	assert(intf);

	struct q931_ie_channel_identification *ie =
		container_of(abstract_ie,
			struct q931_ie_channel_identification, ie);

	if (len < 1) {
		report_ie(abstract_ie, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	int nextoct = 0;
	
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(buf + nextoct++);

	if (oct_3->interface_id_present == Q931_IE_CI_IIP_EXPLICIT) {
		report_ie(abstract_ie, LOG_ERR,
			"IE specifies interface ID thought it"
			" is not supported by the ETSI\n");

		return FALSE;
	}

	ie->interface_id_present = oct_3->interface_id_present;

	if (oct_3->d_channel_indicator == Q931_IE_CI_DCI_IS_D_CHAN) {
		report_ie(abstract_ie, LOG_ERR,
			"IE specifies D channel which"
			" is not supported\n");

		return FALSE;
	}

	ie->preferred_exclusive = oct_3->preferred_exclusive;
	ie->d_channel_indicator = oct_3->d_channel_indicator;
	ie->interface_type = oct_3->interface_type;

	if (ie->interface_type == Q931_IE_CI_IT_PRIMARY) {
		if (len < 2) {
			report_ie(abstract_ie, LOG_ERR, "IE size < 2\n");

			return FALSE;
		}

		if (intf->type == LAPD_INTF_TYPE_BRA) {
			report_ie(abstract_ie, LOG_ERR,
				"IE specifies BRI while interface is PRI\n");

			return FALSE;
		}

		struct q931_ie_channel_identification_onwire_3c *oct_3c =
			(struct q931_ie_channel_identification_onwire_3c *)
			(buf + nextoct++);
		if (oct_3c->number_map == Q931_IE_CI_NM_MAP) {
			report_ie(abstract_ie, LOG_ERR,
				"IE specifies channel map, which"
				" is not supported by DSSS-1\n");

			return FALSE;
		}

		if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
			report_ie(abstract_ie, LOG_ERR,
				"IE specifies unsupported coding type\n");

			return FALSE;
		}

		struct q931_ie_channel_identification_onwire_3d *oct_3d;
		do {
			oct_3d = (struct
				q931_ie_channel_identification_onwire_3d *)
					(buf + nextoct++);

			// FIXME

		} while (!oct_3d->ext);

		return TRUE;

	} else {
		if (intf->type == LAPD_INTF_TYPE_PRA) {
			report_ie(abstract_ie, LOG_ERR,
				"IE specifies PRI while interface is BRI\n");

			return FALSE;
		}

		if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
			q931_chanset_add(&ie->chanset,
					&intf->channels[0]);
		} else if (oct_3->info_channel_selection ==
						Q931_IE_CI_ICS_BRA_B2) {
			q931_chanset_add(&ie->chanset,
					&intf->channels[1]);
		} else if (oct_3->info_channel_selection ==
						Q931_IE_CI_ICS_BRA_ANY) {
			q931_chanset_add(&ie->chanset,
					&intf->channels[0]);
			q931_chanset_add(&ie->chanset,
					&intf->channels[1]);

			if (oct_3->preferred_exclusive ==
						Q931_IE_CI_PE_EXCLUSIVE) {
				report_ie(abstract_ie, LOG_ERR,
					"IE specifies any channel with no"
					" alternative, is this valid?\n");

				return FALSE;
			}
		} else if (oct_3->info_channel_selection ==
					Q931_IE_CI_ICS_BRA_NO_CHANNEL) {
			if (oct_3->preferred_exclusive ==
					Q931_IE_CI_PE_EXCLUSIVE) {
				report_ie(abstract_ie, LOG_ERR,
					"IE specifies no channel with no"
					" alternative, is this valid?\n");

				return FALSE;
			}
		}

		return TRUE;
	}
}

int q931_ie_channel_identification_write_to_buf_bra(
	void *buf,
	const struct q931_ie_channel_identification *ie,
	int max_size)
{
	int len = 0;

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
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
		assert(ie->chanset.chans[0]->id == 0 ||
			ie->chanset.chans[0]->id == 1);
		assert(ie->chanset.chans[1]->id == 0 ||
			ie->chanset.chans[1]->id == 1);

		oct_3->info_channel_selection = Q931_IE_CI_ICS_BRA_ANY;
	} else {
		assert(0);
	}

	len++;

	return len;
}

static int q931_ie_channel_identification_write_to_buf_pra(
	void *buf,
	const struct q931_ie_channel_identification *ie,
	int max_size)
{
	int len = 0;

	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
	oct_3->ext = 1;
	oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	oct_3->interface_type = Q931_IE_CI_IT_PRIMARY;
	oct_3->preferred_exclusive = ie->preferred_exclusive;
	oct_3->d_channel_indicator = ie->d_channel_indicator;
	oct_3->info_channel_selection = Q931_IE_CI_ICS_PRA_NO_CHANNEL; //FIXME
	len++;

	// Interface implicit, do not add Interface identifier

	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		(buf + len);
	oct_3c->raw = 0;
	oct_3c->ext = 1;
	oct_3c->coding_standard = Q931_IE_CI_CS_CCITT;
	oct_3c->number_map = Q931_IE_CI_NM_NUMBER;
	oct_3c->channel_type_map_identifier_type = Q931_IE_CI_ET_B;
	len++;

	struct q931_ie_channel_identification_onwire_3d *oct_3d =
		(struct q931_ie_channel_identification_onwire_3d *)
		(buf + len);
	oct_3d->raw = 0;
	oct_3d->ext = 1;
	len++;

	int i;
	for (i=0; i<ie->chanset.nchans; i++) {
		oct_3d->channel_number = ie->chanset.chans[i]->id;
		len++;
	}

	return len;
}

int q931_ie_channel_identification_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	int len = 0;
	const struct q931_ie_channel_identification *ie =
		container_of(abstract_ie,
			struct q931_ie_channel_identification, ie);

	assert(abstract_ie->cls == my_class);

	if (ie->interface_type == Q931_IE_CI_IT_BASIC) {
		len += q931_ie_channel_identification_write_to_buf_bra(
				buf + len, ie, max_size);
	} else {
		len += q931_ie_channel_identification_write_to_buf_pra(
				buf + len, ie, max_size);
	}

	return len;
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

#if 0
static const char *q931_ie_channel_identification_info_channel_selection_bra_to_text(
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
}
#endif

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
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_channel_identification *ie =
		container_of(abstract_ie,
			struct q931_ie_channel_identification, ie);

	report_ie_dump(abstract_ie,
		"%sInterface id = %s (%d)\n", prefix,
		q931_ie_channel_identification_interface_id_present_to_text(
			ie->interface_id_present),
		ie->interface_id_present);

	report_ie_dump(abstract_ie,
		"%sInterface type = %s (%d)\n", prefix,
		q931_ie_channel_identification_interface_type_to_text(
			ie->interface_type),
		ie->interface_type);

	report_ie_dump(abstract_ie,
		"%sPref/Excl = %s (%d)\n", prefix,
		q931_ie_channel_identification_preferred_exclusive_to_text(
			ie->preferred_exclusive),
		ie->preferred_exclusive);

	report_ie_dump(abstract_ie,
		"%sD channel ident = %s (%d)\n", prefix,
		q931_ie_channel_identification_d_channel_indicator_to_text(
			ie->d_channel_indicator),
		ie->d_channel_indicator);

	report_ie_dump(abstract_ie,
		"%sCoding standard = %s (%d)\n", prefix,
		q931_ie_channel_identification_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	char chanlist[128] = "";

	int i;
	for (i=0; i<ie->chanset.nchans; i++) {
		sprintf(chanlist + strlen(chanlist),
			"B%d ",
			ie->chanset.chans[i]->id + 1);
	}

	report_ie_dump(abstract_ie,
		"%sChannels = %s\n",
		prefix,
		chanlist);
}

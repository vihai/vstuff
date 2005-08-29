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
#include <libq931/intf.h>
#include <libq931/message.h>

#include <libq931/ie_restind.h>

static const struct q931_ie_type *ie_type;

void q931_ie_restart_indicator_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_restart_indicator *q931_ie_restart_indicator_alloc(void)
{
	struct q931_ie_restart_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.type = ie_type;
	ie->ie.refcnt = 1;

	return ie;
}

struct q931_ie *q931_ie_restart_indicator_alloc_abstract(void)
{
	return &q931_ie_restart_indicator_alloc()->ie;
}

int q931_ie_restart_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_restart_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_restart_indicator, ie);

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
		(struct q931_ie_restart_indicator_onwire_3 *)
		(msg->rawies + pos + 0);

	if (oct_3->ext != 1) {
		report_msg(msg, LOG_ERR, "Ext != 1\n");
		return FALSE;
	}

	if (oct_3->restart_class != Q931_IE_RI_C_INDICATED &&
	    oct_3->restart_class != Q931_IE_RI_C_SINGLE_INTERFACE &&
	    oct_3->restart_class != Q931_IE_RI_C_ALL_INTERFACES) {
		report_msg(msg, LOG_ERR,
			"IE specifies invalid class\n");

		return FALSE;
	}

	ie->restart_class = oct_3->restart_class;

	return TRUE;
}

int q931_ie_restart_indicator_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_restart_indicator *ie =
		container_of(abstract_ie, struct q931_ie_restart_indicator, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_RESTART_INDICATOR;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
	  (struct q931_ie_restart_indicator_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->restart_class = ie->restart_class;
	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_restart_indicator_restart_class_to_text(
	enum q931_ie_restart_indicator_restart_class class)
{
	switch(class) {
	case Q931_IE_RI_C_INDICATED:
		return "Indicated";
	case Q931_IE_RI_C_SINGLE_INTERFACE:
		return "Single interface";
	case Q931_IE_RI_C_ALL_INTERFACES:
		return "All interfaces";
	default:
		return "*INVALID*";
	}
}

void q931_ie_restart_indicator_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_restart_indicator *ie =
		container_of(generic_ie, struct q931_ie_restart_indicator, ie);

	report(LOG_DEBUG, "%sDescription = %s (%d)\n", prefix,
		q931_ie_restart_indicator_restart_class_to_text(
			ie->restart_class),
		ie->restart_class);
}

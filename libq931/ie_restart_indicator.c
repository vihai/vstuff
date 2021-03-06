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

#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/intf.h>
#include <libq931/message.h>

#include <libq931/ie_restart_indicator.h>

static const struct q931_ie_class *my_class;

void q931_ie_restart_indicator_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_restart_indicator *q931_ie_restart_indicator_alloc(void)
{
	struct q931_ie_restart_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.cls = my_class;
	ie->ie.refcnt = 1;

	return ie;
}

struct q931_ie *q931_ie_restart_indicator_alloc_abstract(void)
{
	return &q931_ie_restart_indicator_alloc()->ie;
}

int q931_ie_restart_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	struct q931_ie_restart_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_restart_indicator, ie);

	if (len < 1) {
		report_ie(abstract_ie, LOG_ERR, "IE size < 1\n");
		return FALSE;
	}

	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
		(struct q931_ie_restart_indicator_onwire_3 *)
		(buf + 0);

	if (oct_3->ext != 1) {
		report_ie(abstract_ie, LOG_ERR, "Ext != 1\n");
		return FALSE;
	}

	if (oct_3->restart_class != Q931_IE_RI_C_INDICATED &&
	    oct_3->restart_class != Q931_IE_RI_C_SINGLE_INTERFACE &&
	    oct_3->restart_class != Q931_IE_RI_C_ALL_INTERFACES) {
		report_ie(abstract_ie, LOG_ERR,
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
	int len = 0;
	struct q931_ie_restart_indicator *ie =
		container_of(abstract_ie, struct q931_ie_restart_indicator, ie);

	struct q931_ie_restart_indicator_onwire_3 *oct_3 =
		(struct q931_ie_restart_indicator_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
	oct_3->ext = 1;
	oct_3->restart_class = ie->restart_class;
	len++;

	return len;
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
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_restart_indicator *ie =
		container_of(abstract_ie, struct q931_ie_restart_indicator, ie);

	report_ie_dump(abstract_ie,
		"%sClass = %s (%d)\n", prefix,
		q931_ie_restart_indicator_restart_class_to_text(
			ie->restart_class),
		ie->restart_class);
}

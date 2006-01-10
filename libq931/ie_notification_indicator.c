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
#include <libq931/ie_notification_indicator.h>

static const struct q931_ie_type *ie_type;

void q931_ie_notification_indicator_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_notification_indicator *
	q931_ie_notification_indicator_alloc(void)
{
	struct q931_ie_notification_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_notification_indicator_alloc_abstract(void)
{
	return &q931_ie_notification_indicator_alloc()->ie;
}

int q931_ie_notification_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_notification_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_notification_indicator, ie);

	if (len < 2) {
		report_msg(msg, LOG_ERR, "IE len < 2\n");
		return FALSE;
	}

	struct q931_ie_notification_indicator_onwire_3 *oct_3 =
		(struct q931_ie_notification_indicator_onwire_3 *)
		(msg->rawies + pos);

	if (!oct_3->ext) {
		report_msg(msg, LOG_WARNING, "IE oct 3 ext != 0\n");
		return FALSE;
	}

	ie->description = oct_3->description;

	return TRUE;
}

int q931_ie_notification_indicator_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_notification_indicator *ie =
		container_of(generic_ie,
			struct q931_ie_notification_indicator, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_NOTIFICATION_INDICATOR;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_notification_indicator_onwire_3 *oct_3 =
	  	(struct q931_ie_notification_indicator_onwire_3 *)
		(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->description = ie->description;
	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_notification_indicator_description_to_text(
	enum q931_ie_notification_indicator_description description)
{
	switch(description) {
	case Q931_IE_NI_D_USER_SUSPENDED:
		return "User suspended";
	case Q931_IE_NI_D_USER_RESUMED:
		return "User resumed";
	case Q931_IE_NI_D_BEARER_SERVICE_CHANGE:
	        return "Bearer service change";
	default:
		return "*INVALID*";
	}
}

void q931_ie_notification_indicator_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_notification_indicator *ie =
		container_of(generic_ie,
			struct q931_ie_notification_indicator, ie);

	report(LOG_DEBUG, "%sNotification Indicator = %s (%d)\n", prefix,
		q931_ie_notification_indicator_description_to_text(
			ie->description),
		ie->description);
}

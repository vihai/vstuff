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

static const struct q931_ie_class *my_class;

void q931_ie_notification_indicator_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_notification_indicator *
	q931_ie_notification_indicator_alloc(void)
{
	struct q931_ie_notification_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.cls = my_class;

	return ie;
}

struct q931_ie *q931_ie_notification_indicator_alloc_abstract(void)
{
	return &q931_ie_notification_indicator_alloc()->ie;
}

int q931_ie_notification_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	struct q931_ie_notification_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_notification_indicator, ie);

	if (len != 1) {
		report_ie(abstract_ie, LOG_ERR, "IE len != 1\n");
		return FALSE;
	}

	struct q931_ie_notification_indicator_onwire_3 *oct_3 =
		(struct q931_ie_notification_indicator_onwire_3 *)
		(buf + 0);

	if (!oct_3->ext) {
		report_ie(abstract_ie, LOG_WARNING, "IE oct 3 ext != 0\n");
		return FALSE;
	}

	ie->description = oct_3->description;

	return TRUE;
}

int q931_ie_notification_indicator_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	int len = 0;
	struct q931_ie_notification_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_notification_indicator, ie);

	struct q931_ie_notification_indicator_onwire_3 *oct_3 =
	  	(struct q931_ie_notification_indicator_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
	oct_3->ext = 1;
	oct_3->description = ie->description;
	len++;

	return len;
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
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_notification_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_notification_indicator, ie);

	report_ie_dump(abstract_ie,
		"%sNotification Indicator = %s (%d)\n", prefix,
		q931_ie_notification_indicator_description_to_text(
			ie->description),
		ie->description);
}

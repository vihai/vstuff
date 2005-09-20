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
#include <libq931/ie_display.h>

static const struct q931_ie_type *ie_type;

void q931_ie_display_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_display *q931_ie_display_alloc(void)
{
	struct q931_ie_display *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_display_alloc_abstract(void)
{
	return &q931_ie_display_alloc()->ie;
}

int q931_ie_display_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_display *ie =
		container_of(abstract_ie,
			struct q931_ie_display, ie);

	if (len < 1) {
		report_msg(msg, LOG_ERR, "IE len < 1\n");
		return FALSE;
	}

	if (len > 82) {
		// Be reeeeally sure the IE is not > 82 octets
		report_msg(msg, LOG_ERR, "IE len > 82\n");
		return FALSE;
	}

	memcpy(ie->text, msg->rawies + pos, len);
	ie->text[len] = '\0';

	return TRUE;
}

int q931_ie_display_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_display *ie =
		container_of(generic_ie, struct q931_ie_display, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_DISPLAY;
	ieow->len = 0;

	memcpy(ieow->data, ie->text, strlen(ie->text));
	ieow->len += strlen(ie->text);

	return ieow->len + sizeof(struct q931_ie_onwire);
}

void q931_ie_display_dump(
	const struct q931_ie *generic_ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_display *ie =
		container_of(generic_ie, struct q931_ie_display, ie);

	char sane_str[10];
	int i;
	for(i=0; i<sizeof(sane_str); i++) {
		sane_str[i] = isprint(ie->text[i]) ? ie->text[i] : '.';
	}
	sane_str[i] = '\0';

	report(LOG_DEBUG, "%sDisplay = %s (%d)\n", prefix,
		sane_str);
}

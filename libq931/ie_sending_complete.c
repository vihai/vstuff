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
#include <libq931/ie_sending_complete.h>

static const struct q931_ie_class *my_class;

void q931_ie_sending_complete_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_sending_complete *q931_ie_sending_complete_alloc(void)
{
	struct q931_ie_sending_complete *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.cls = my_class;
	ie->ie.refcnt = 1;

	return ie;
}

struct q931_ie *q931_ie_sending_complete_alloc_abstract(void)
{
	return &q931_ie_sending_complete_alloc()->ie;
}

int q931_ie_sending_complete_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	return TRUE;
}

int q931_ie_sending_complete_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	*(__u8 *)buf = Q931_IE_SENDING_COMPLETE;

	return 1;
}

void q931_ie_sending_complete_dump(
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
}

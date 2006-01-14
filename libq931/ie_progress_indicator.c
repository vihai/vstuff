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
#include <libq931/ie_progress_indicator.h>

static const struct q931_ie_class *my_class;

void q931_ie_progress_indicator_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;
}

struct q931_ie_progress_indicator *q931_ie_progress_indicator_alloc(void)
{
	struct q931_ie_progress_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.cls = my_class;

	return ie;
}

struct q931_ie *q931_ie_progress_indicator_alloc_abstract(void)
{
	return &q931_ie_progress_indicator_alloc()->ie;
}

int q931_ie_progress_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	struct q931_ie_progress_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_progress_indicator, ie);

	int nextoct = 0;

	if (len < 2) {
		report_ie(abstract_ie, LOG_ERR, "IE size < 2\n");
		return FALSE;
	}

	struct q931_ie_progress_indicator_onwire_3 *oct_3 =
		(struct q931_ie_progress_indicator_onwire_3 *)
		(buf + nextoct++);

	if (oct_3->ext == 0) {
		report_ie(abstract_ie, LOG_ERR, "IE oct-3 ext != 1\n");
		return FALSE;
	}

	ie->coding_standard = oct_3->coding_standard;
	ie->location = oct_3->location;

	struct q931_ie_progress_indicator_onwire_4 *oct_4 =
		(struct q931_ie_progress_indicator_onwire_4 *)
		(buf + nextoct++);

	if (oct_4->ext == 0) {
		report_ie(abstract_ie, LOG_ERR, "IE oct-4 ext != 1\n");
		return FALSE;
	}

	ie->progress_description = oct_4->progress_description;

	return TRUE;
}

int q931_ie_progress_indicator_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	int len = 0;
	struct q931_ie_progress_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_progress_indicator, ie);

	struct q931_ie_progress_indicator_onwire_3 *oct_3 =
		(struct q931_ie_progress_indicator_onwire_3 *)
		(buf + len);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->location = ie->location;
	len++;

	struct q931_ie_progress_indicator_onwire_4 *oct_4 =
		(struct q931_ie_progress_indicator_onwire_4 *)
		(buf + len);
	oct_4->raw = 0;
	oct_4->ext = 1;
	oct_4->progress_description = ie->progress_description;
	len++;

	return len;
}

enum q931_ie_progress_indicator_location
	q931_ie_progress_indicator_location(
		const struct q931_call *call)
{
	if (call->intf->network_role == Q931_INTF_NET_USER) {
		return Q931_IE_PI_L_USER;
	} else if (call->intf->network_role == Q931_INTF_NET_PRIVATE) {
		if (call->intf->role == LAPD_ROLE_NT) {
			if (call->direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_LOCAL_USER;
			else
				return Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
		} else {
			if (call->direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			else
				return Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_LOCAL_USER;
		}
	} else if (call->intf->network_role == Q931_INTF_NET_LOCAL ||
	           call->intf->network_role == Q931_INTF_NET_TRANSIT) {
		if (call->intf->role == LAPD_ROLE_NT) {
			if (call->direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_LOCAL_USER;
			else
				return Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_REMOTE_USER;
		} else {
			if (call->direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_REMOTE_USER;
			else
				return Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_LOCAL_USER;
		}
	} else if (call->intf->network_role == Q931_INTF_NET_INTERNATIONAL) {
		return Q931_IE_PI_L_INTERNATIONAL_NETWORK;
	}

	assert(0);
	return 0;
}

static const char *q931_ie_progress_indicator_coding_standard_to_text(
	enum q931_ie_progress_indicator_coding_standard coding_standard)
{
	switch(coding_standard) {
	case Q931_IE_PI_CS_CCITT:
		return "CCITT";
	case Q931_IE_PI_CS_RESERVED:
		return "Reserved";
	case Q931_IE_PI_CS_NATIONAL:
		return "National";
	case Q931_IE_PI_CS_NETWORK_SPECIFIC:
		return "Specific";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_progress_indicator_location_to_text(
	enum q931_ie_progress_indicator_location location)
{
	switch(location) {
	case Q931_IE_PI_L_USER:
		return "User";
	case Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_LOCAL_USER:
		return "Private network serving local user";
	case Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_LOCAL_USER:
		return "Public network serving local user";
	case Q931_IE_PI_L_PUBLIC_NETWORK_SERVING_REMOTE_USER:
		return "Public network serving remote user";
	case Q931_IE_PI_L_PRIVATE_NETWORK_SERVING_REMOTE_USER:
		return "Private network serving remote user";
	case Q931_IE_PI_L_INTERNATIONAL_NETWORK:
		return "International network";
	case Q931_IE_PI_L_NETWORK_BEYOND_INTERNETWORKING_POINT:
		return "Network beyond internetworking point";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_progress_indicator_progress_description_to_text(
	enum q931_ie_progress_indicator_progress_description progress_description)
{
	switch(progress_description) {
	case Q931_IE_PI_PD_CALL_NOT_END_TO_END:
		return "Call is not end-to-end";
	case Q931_IE_PI_PD_DESTINATION_ADDRESS_IS_NON_ISDN:
		return "Destination address is non-ISDN";
	case Q931_IE_PI_PD_ORIGINATION_ADDRESS_IS_NON_ISDN:
		return "Origination address is non-ISDN";
	case Q931_IE_PI_PD_CALL_HAS_RETURNED_TO_THE_ISDN:
		return "Call has returned to the ISDN";
	case Q931_IE_PI_PD_IN_BAND_INFORMATION:
		return "In-band information or appropriate pattern now available";
	default:
		return "*INVALID*";
	}
}

void q931_ie_progress_indicator_dump(
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_progress_indicator *ie =
		container_of(abstract_ie, struct q931_ie_progress_indicator, ie);

	report_ie_dump(abstract_ie,
		"%sCoding standard = %s (%d)\n", prefix,
		q931_ie_progress_indicator_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	report_ie_dump(abstract_ie,
		"%sLocation = %s (%d)\n", prefix,
		q931_ie_progress_indicator_location_to_text(
			ie->location),
		ie->location);

	report_ie_dump(abstract_ie,
		"%sDescription = %s (%d)\n", prefix,
		q931_ie_progress_indicator_progress_description_to_text(
			ie->progress_description),
		ie->progress_description);
}

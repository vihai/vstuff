#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ie_progind.h"

static const struct q931_ie_type *ie_type;

void q931_ie_progress_indicator_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_progress_indicator *q931_ie_progress_indicator_alloc(void)
{
	struct q931_ie_progress_indicator *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_progress_indicator_alloc_abstract(void)
{
	return &q931_ie_progress_indicator_alloc()->ie;
}

int q931_ie_progress_indicator_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_progress_indicator *ie =
		container_of(abstract_ie,
			struct q931_ie_progress_indicator, ie);

	int nextoct = 0;

	if (len < 2) {
		report_msg(msg, LOG_ERR, "IE size < 2\n");
		return FALSE;
	}

	struct q931_ie_progress_indicator_onwire_3 *oct_3 =
		(struct q931_ie_progress_indicator_onwire_3 *)
		(msg->rawies + pos + (nextoct++));

	if (oct_3->ext == 0) {
		report_msg(msg, LOG_ERR, "IE oct-3 ext != 1\n");
		return FALSE;
	}

	ie->coding_standard = oct_3->coding_standard;
	ie->location = oct_3->location;

	struct q931_ie_progress_indicator_onwire_4 *oct_4 =
		(struct q931_ie_progress_indicator_onwire_4 *)
		(msg->rawies + pos + (nextoct++));

	if (oct_4->ext == 0) {
		report_msg(msg, LOG_ERR, "IE oct-4 ext != 1\n");
		return FALSE;
	}

	ie->progress_description = oct_4->progress_description;

	return TRUE;
}

int q931_ie_progress_indicator_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_progress_indicator *ie =
		container_of(generic_ie, struct q931_ie_progress_indicator, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_PROGRESS_INDICATOR;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_progress_indicator_onwire_3 *oct_3 =
	  (struct q931_ie_progress_indicator_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->location = ie->location;
	ieow->len++;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_progress_indicator_onwire_4 *oct_4 =
	  (struct q931_ie_progress_indicator_onwire_4 *)(&ieow->data[ieow->len]);
	oct_4->ext = 1;
	oct_4->progress_description = ie->progress_description;
	ieow->len++;

	return ieow->len + sizeof(struct q931_ie_onwire);
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


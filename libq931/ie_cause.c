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
#include <stdlib.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/intf.h>
#include <libq931/message.h>
#include <libq931/ie_cause.h>

static const struct q931_ie_class *my_class;

void q931_ie_cause_register(
	const struct q931_ie_class *ie_class)
{
	my_class = ie_class;

	q931_ie_cause_value_infos_init();
}

struct q931_ie_cause *q931_ie_cause_alloc(void)
{
	struct q931_ie_cause *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.cls = my_class;

	return ie;
}

struct q931_ie *q931_ie_cause_alloc_abstract(void)
{
	return &q931_ie_cause_alloc()->ie;
}

int q931_ie_cause_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf)
{
	assert(abstract_ie->cls == my_class);

	struct q931_ie_cause *ie =
		container_of(abstract_ie,
			struct q931_ie_cause, ie);

	int nextoct = 0;

	if (len < 2) {
		report_ie(abstract_ie, LOG_ERR, "IE size < 2\n");

		return FALSE;
	}

	struct q931_ie_cause_onwire_3 *oct_3 =
		(struct q931_ie_cause_onwire_3 *)
		(buf + nextoct);
	nextoct++;

	ie->coding_standard = oct_3->coding_standard;

	if (oct_3->coding_standard != Q931_IE_C_CS_CCITT) {
		// What should we do?
		report_ie(abstract_ie, LOG_WARNING,
			"What should we do if coding_standard != CCITT?\n");
	}

	if (oct_3->ext == 0) {
		struct q931_ie_cause_onwire_3a *oct_3a =
			(struct q931_ie_cause_onwire_3a *)
			(buf + nextoct);
		nextoct++;

		if (oct_3a->ext != 1) {
			report_ie(abstract_ie, LOG_ERR, "Extension bit unexpectedly set to 0\n");
			return FALSE;
		}

		if (oct_3a->recommendation != Q931_IE_C_R_Q931) {
			report_ie(abstract_ie, LOG_ERR,
				"Recommendation unexpectedly != Q.931\n");

			return FALSE;
		}
	}

	ie->recommendation = Q931_IE_C_R_Q931;

	struct q931_ie_cause_onwire_4 *oct_4 =
		(struct q931_ie_cause_onwire_4 *)
		(buf + nextoct);
	nextoct++;

	ie->value = oct_4->cause_value;

	return TRUE;
}


int q931_ie_cause_write_to_buf(
	const struct q931_ie *abstract_ie,
	void *buf,
	int max_size)
{
	int len = 0;
	struct q931_ie_cause *ie =
		container_of(abstract_ie, struct q931_ie_cause, ie);

	struct q931_ie_cause_onwire_3 *oct_3 =
		(struct q931_ie_cause_onwire_3 *)
		(buf + len);
	oct_3->raw = 0;
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->location = ie->location;
	len++;

	struct q931_ie_cause_onwire_4 *oct_4 =
		(struct q931_ie_cause_onwire_4 *)
		(buf + len);
	oct_4->raw = 0;
	oct_4->ext = 1;
	oct_4->cause_value = ie->value;
	len++;

	memcpy(buf + len, ie->diagnostics, ie->diagnostics_len);
	len += ie->diagnostics_len;

	return len;
}

int q931_ies_contain_cause(
	const struct q931_ies *ies,
	enum q931_ie_cause_value cause_value)
{
	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CAUSE) {
			struct q931_ie_cause *cause =
				container_of(ies->ies[i],
					struct q931_ie_cause, ie);

			if (cause->value == cause_value)
				return TRUE;
		}
	}

	return FALSE;
}

enum q931_ie_cause_location q931_ie_cause_location(
	enum q931_call_direction direction,
	enum q931_interface_network_role network_role,
	enum lapd_role role)
{
	if (network_role == Q931_INTF_NET_USER) {
		return Q931_IE_C_L_USER;
	} else if (network_role == Q931_INTF_NET_PRIVATE) {
		if (role == LAPD_ROLE_NT) {
			if (direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_C_L_PRIVATE_NETWORK_SERVING_LOCAL_USER;
			else
				return Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
		} else {
			if (direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			else
				return Q931_IE_C_L_PRIVATE_NETWORK_SERVING_LOCAL_USER;
		}
	} else if (network_role == Q931_INTF_NET_LOCAL) {
		if (role == LAPD_ROLE_NT) {
			if (direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_C_L_PUBLIC_NETWORK_SERVING_LOCAL_USER;
			else
				return Q931_IE_C_L_PUBLIC_NETWORK_SERVING_REMOTE_USER;
		} else {
			if (direction == Q931_CALL_DIRECTION_INBOUND)
				return Q931_IE_C_L_PUBLIC_NETWORK_SERVING_REMOTE_USER;
			else
				return Q931_IE_C_L_PUBLIC_NETWORK_SERVING_LOCAL_USER;
		}
	} else if (network_role == Q931_INTF_NET_TRANSIT) {
		return Q931_IE_C_L_TRANSIT_NETWORK;
	} else if (network_role == Q931_INTF_NET_INTERNATIONAL) {
		return Q931_IE_C_L_INTERNATIONAL_NETWORK;
	}

	assert(0);
	return 0;
}

static const char *q931_ie_cause_coding_standard_to_text(
	enum q931_ie_cause_coding_standard coding_standard)
{
	switch(coding_standard) {
	case Q931_IE_C_CS_CCITT:
		return "CCITT";
	case Q931_IE_C_CS_RESERVED:
		return "Reserved";
	case Q931_IE_C_CS_NATIONAL:
		return "National";
	case Q931_IE_C_CS_SPECIFIC:
		return "Specific";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_cause_location_to_text(
	enum q931_ie_cause_location location)
{
	switch(location) {
	case Q931_IE_C_L_USER:
		return "User";
	case Q931_IE_C_L_PRIVATE_NETWORK_SERVING_LOCAL_USER:
		return "Private network serving local user";
	case Q931_IE_C_L_PUBLIC_NETWORK_SERVING_LOCAL_USER:
		return "Public network serving local user";
	case Q931_IE_C_L_TRANSIT_NETWORK:
		return "Transit network";
	case Q931_IE_C_L_PUBLIC_NETWORK_SERVING_REMOTE_USER:
		return "Public network serving remote user";
	case Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER:
		return "Private network serving remote user";
	case Q931_IE_C_L_INTERNATIONAL_NETWORK:
		return "International network";
	case Q931_IE_C_L_NETWORK_BEYOND_INTERNETWORKING_POINT:
		return "Network beyond internetworking point";
	default:
		return "*INVALID*";
	}
}

struct q931_ie_cause_value_info q931_ie_cause_value_infos[] =
{
	{
	Q931_IE_C_CV_UNALLOCATED_NUMBER,
	"Unallocated",
	},
	{
	Q931_IE_C_CV_NO_ROUTE_TO_SPECIFIED_TRANSIT_NETWORK,
	"No route to specified transit network",
	},
	{
	Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION,
	"No route to destination",
	},
	{
	Q931_IE_C_CV_CHANNEL_UNACCEPTABLE,
	"Channel unacceptable",
	},
	{
	Q931_IE_C_CV_CALL_BEING_DELIVERED,
	"Call awarded and being delivered in an established channel",
	},
	{
	Q931_IE_C_CV_NORMAL_CALL_CLEARING,
	"Normal call clearing",
	},
	{
	Q931_IE_C_CV_USER_BUSY,
	"User busy",
	},
	{
	Q931_IE_C_CV_NO_USER_RESPONDING,
	"No user responding",
	},
	{
	Q931_IE_C_CV_NO_ANSWER_FROM_USER,
	"No answer from user (user alerted)",
	},
	{
	Q931_IE_C_CV_CALL_REJECTED,
	"Call rejected",
	},
	{
	Q931_IE_C_CV_NUMBER_CHANGED,
	"Number changed",
	},
	{
	Q931_IE_C_CV_NON_SELECTED_USER_CLEARING,
	"Non-selected user clearing",
	},
	{
	Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER,
	"Destination out of order",
	},
	{
	Q931_IE_C_CV_INVALID_NUMBER_FORMAT,
	"Invalid number format",
	},
	{
	Q931_IE_C_CV_FACILITY_REJECTED,
	"Facility rejected",
	},
	{
	Q931_IE_C_CV_RESPONSE_TO_STATUS_ENQUIRY,
	"Response to STATUS ENQUIRY",
	},
	{
	Q931_IE_C_CV_NORMAL_UNSPECIFIED,
	"Normal, unspecified",
	},

	{
	Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE,
	"No circuit/channel available",
	},
	{
	Q931_IE_C_CV_NETWORK_OUT_OF_ORDER,
	"Network out of order",
	},
	{
	Q931_IE_C_CV_TEMPORARY_FAILURE,
	"Temporary failure",
	},
	{
	Q931_IE_C_CV_SWITCHING_EQUIPMENT_CONGESTION,
	"Switching equipment congestion",
	},
	{
	Q931_IE_C_CV_ACCESS_INFORMATION_DISCARDED,
	"Access information discarded",
	},
	{
	Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE,
	"Requested circuit/channel not available",
	},
	{
	Q931_IE_C_CV_RESOURCES_UNAVAILABLE,
	"Resources unavailable, unspecified",
	},

	{
	Q931_IE_C_CV_QUALITY_OF_SERVICE_UNAVAILABLE,
	"Quality of service unavailable",
	},
	{
	Q931_IE_C_CV_REQUESTED_FACILITY_NOT_SUBSCRIBED,
	"Requested facility not subscribed",
	},
	{
	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_AUTHORIZED,
	"Bearer capability not authorized",
	},
	{
	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_PRESENTLY_AVAILABLE,
	"Bearer capability not presently available",
	},
	{
	Q931_IE_C_CV_SERVICE_OR_OPTION_NOT_AVAILABLE,
	"Service or option not available, unspecified",
	},

	{
	Q931_IE_C_CV_BEARER_CAPABILITY_NOT_IMPLEMENTED,
	"Bearer capability not implemented",
	},
	{
	Q931_IE_C_CV_CHANNEL_TYPE_NOT_IMPLEMENTED,
	"Channel type not implemented",
	},
	{
	Q931_IE_C_CV_REQUESTED_FACILITY_NOT_IMPLEMENTED,
	"Requested facility not implemented",
	},
	{
	Q931_IE_C_CV_ONLY_RESTRICTED_DIGITAL_AVAILABLE,
	"Only restricted digital information bearer capability is available",
	},
	{
	Q931_IE_C_CV_SERVICE_OR_OPTION_NOT_IMPLEMENTED,
	"Service or option not implemented, unspecified",
	},

	{
	Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE,
	"Invalid call reference value",
	},
	{
	Q931_IE_C_CV_IDENTIFIED_CHANNEL_DOES_NOT_EXIST,
	"Identified channel does not exist",
	},
	{
	Q931_IE_C_CV_SUSPENDED_CALL_EXISTS_BUT_NOT_THIS,
	"A suspended call exists, but this call identity does not",
	},
	{
	Q931_IE_C_CV_CALL_IDENITY_IN_USE,
	"Call identity in use",
	},
	{
	Q931_IE_C_CV_NO_CALL_SUSPENDED,
	"No call suspended",
	},
	{
	Q931_IE_C_CV_CALL_IDENTIFIED_HAS_BEEN_CLEARED,
	"Call hafing the requested identity has been cleared",
	},
	{
	Q931_IE_C_CV_INCOMPATIBLE_DESTINATION,
	"Incompatible destination",
	},
	{
	Q931_IE_C_CV_INVALID_TRANSIT_NETWORK_SELECTION,
	"Invalid transit network selection",
	},
	{
	Q931_IE_C_CV_INVALID_MESSAGE_UNSPECIFIED,
	"Invalid message, unspecified",
	},

	{
	Q931_IE_C_CV_MANDATORY_INFORMATION_ELEMENT_IS_MISSING,
	"Mandatory information element is missing",
	},
	{
	Q931_IE_C_CV_MESSAGE_TYPE_NON_EXISTENT_OR_IMPLEMENTED,
	"Message type non-existent or not implemented",
	},
	{
	Q931_IE_C_CV_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_STATE,
	"Message not compatible with call state or message"
	" type non-existent or not implemented",
	},
	{
	Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT,
	"Information element non-existend or not implemented",
	},
	{
	Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS,
	"Invalid information element contents",
	},
	{
	Q931_IE_C_CV_MESSAGE_NOT_COMPATIBLE_WITH_CALL_STATE,
	"Message not compatible with call state",
	},
	{
	Q931_IE_C_CV_RECOVERY_ON_TIMER_EXPIRY,
	"Recovery on timer expiry",
	},
	{
	Q931_IE_C_CV_PROTOCOL_ERROR_UNSPECIFIED,
	"Protocol error, unspecified",
	},

	{
	Q931_IE_C_CV_INTERWORKING_UNSPECIFIED,
	"Interworking, unspecified",
	},
};

static int q931_ie_cause_value_compare(const void *a, const void *b)
{
	return q931_intcmp(((struct q931_ie_cause_value_info *)a)->value,
	                   ((struct q931_ie_cause_value_info *)b)->value);
}

const struct q931_ie_cause_value_info *q931_get_ie_cause_value_info(int value)
{
	struct q931_ie_cause_value_info key, *res;

	key.value = value;
	key.name = NULL;

	res = (struct q931_ie_cause_value_info *)
	      bsearch(&key,
	        q931_ie_cause_value_infos,
	        sizeof(q931_ie_cause_value_infos)/
	          sizeof(struct q931_ie_cause_value_info),
	        sizeof(struct q931_ie_cause_value_info),
	        q931_ie_cause_value_compare);

	return res;
}

void q931_ie_cause_value_infos_init()
{
	qsort(q931_ie_cause_value_infos,
	      sizeof(q931_ie_cause_value_infos)/
	        sizeof(struct q931_ie_cause_value_info),
	      sizeof(struct q931_ie_cause_value_info),
	      q931_ie_cause_value_compare);
}


static const char *q931_ie_cause_cause_value_to_text(
	enum q931_ie_cause_value cause_value)
{
	const struct q931_ie_cause_value_info *info =
		q931_get_ie_cause_value_info(cause_value);

	if (info)
		return info->name;
	else
		return "*INVALID*";
}

void q931_ie_cause_dump(
	const struct q931_ie *abstract_ie,
	void (*report_func)(int level, const char *format, ...),
	const char *prefix)
{
	struct q931_ie_cause *ie =
		container_of(abstract_ie, struct q931_ie_cause, ie);

	report_ie_dump(abstract_ie,
		"%sCoding standard = %s (%d)\n", prefix,
		q931_ie_cause_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	report_ie_dump(abstract_ie,
		"%sLocation = %s (%d)\n", prefix,
		q931_ie_cause_location_to_text(
			ie->location),
		ie->location);

	report_ie_dump(abstract_ie,
		"%sCause value = %s (%d)\n", prefix,
		q931_ie_cause_cause_value_to_text(
			ie->value),
		ie->value);
}


#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "intf.h"
#include "message.h"
#include "ie_cause.h"

static const struct q931_ie_type *ie_type;

void q931_ie_cause_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
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
	Q931_IE_C_CV_NO_CIRCUIT_CANNEL_AVAILABLE,
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
	"Rquested circuit/channel not available",
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

int q931_ie_cause_check(
	const struct q931_ie *ie,
	const struct q931_message *msg)
{
	int nextoct = 0;

	if (ie->len < 1) {
		report_msg(msg, LOG_ERR, "IE size < 1\n");

		return FALSE;
	}

	struct q931_ie_cause_onwire_3 *oct_3 =
		(struct q931_ie_cause_onwire_3 *)
		(ie->data + nextoct);

	if (oct_3->coding_standard != Q931_IE_C_CS_CCITT) {
		report_msg(msg, LOG_ERR, "coding stanrdard != CCITT\n");

		return FALSE;
	}

	if (oct_3->ext == 0) {
		nextoct++;

		struct q931_ie_cause_onwire_3a *oct_3a =
			(struct q931_ie_cause_onwire_3a *)
			(ie->data + nextoct);

		if (oct_3a->ext != 1) {
			report_msg(msg, LOG_ERR,
				"Extension bit unexpectedly set to 0\n");

			return FALSE;
		}

		if (oct_3a->recommendation != Q931_IE_C_R_Q931) {
			report_msg(msg, LOG_ERR,
				"Recommendation unexpectedly != Q.931\n");

			return FALSE;
		}
	}

	return TRUE;
}


static int q931_ie_cause_value_compare(const void *a, const void *b)
{
	return q931_intcmp(((struct q931_ie_cause_value_info *)a)->id,
	                   ((struct q931_ie_cause_value_info *)b)->id);
}

const struct q931_ie_cause_value_info *q931_get_ie_cause_value_info(int id)
{
	struct q931_ie_cause_value_info key, *res;

	key.id = id;
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

/*
void q931_ie_cause_add_to_causeset(
	const struct q931_ie *ie,
	struct q931_causeset *causeset)
{
	assert(ie);
	assert(ie->info);
	assert(ie->info->id == Q931_IE_CAUSE);
	assert(causeset);

	causeset->ncauses = 0;

	int nextoct = 0;
	struct q931_ie_cause_onwire_3 *oct_3 =
		(struct q931_ie_cause_onwire_3 *)
		(ie->data + nextoct);

	nextoct++;

	if (oct_3->ext == 0)
		nextoct++;

	struct q931_ie_cause_onwire_4 *oct_4 =
		(struct q931_ie_cause_onwire_4 *)
		(ie->data + nextoct);

	nextoct++;

	q931_causeset_add_diag(causeset,
		oct_4->cause_value,
		ie->data + nextoct,
		ie->len - nextoct);
}
*/

int q931_ie_cause_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_cause *ie =
		container_of(generic_ie, struct q931_ie_cause, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_CAUSE;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_cause_onwire_3 *oct_3 =
	  (struct q931_ie_cause_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->location = ie->location;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_cause_onwire_4 *oct_4 =
	  (struct q931_ie_cause_onwire_4 *)(&ieow->data[ieow->len]);
	oct_4->ext = 1;
	oct_4->cause_value = ie->value;
	ieow->len += 1;

	memcpy(&ieow->data[ieow->len], ie->diagnostics, ie->diagnostics_len);
	ieow->len += ie->diagnostics_len;

	return ieow->len + sizeof(struct q931_ie_onwire);
}

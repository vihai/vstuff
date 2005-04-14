#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include "q931.h"
#include "ie.h"

static struct q931_ie_info q931_ie_infos[] =
{
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_SHIFT,
		"Shift",
		NULL,
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_CONGESTION_LEVEL,
		"Congestion Level",
		NULL,
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_MORE_DATA,
		"More Data",
		NULL,
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_SENDING_COMPLETE,
		"Sending Complete",
		NULL,
	},
	{
		1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REPEAT_INDICATOR,
		"Repeat Indicator",
		NULL,
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_SEGMENTED_MESSAGE,
		"Segmented Message",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CHANGE_STATUS,
		"Change Status",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_SPECIAL,
		"Special",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CONNECTED_ADDRESS,
		"Connected Address",
		NULL,
	},
	{
		13,
		1,
		Q931_NT_ETSI,
		Q931_IE_BEARER_CAPABILITY,
		"Bearer Capability",
		NULL,
	},
	{
		32,
		1,
		Q931_NT_ETSI,
		Q931_IE_CAUSE,
		"Cause",
		q931_ie_cause_check,
	},
	{
		10,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALL_IDENTITY,
		"Call Identity",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALL_STATE,
		"Call State",
		q931_ie_call_state_check,
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_CHANNEL_IDENTIFICATION,
		"Channel Identification",
		q931_ie_channel_identification_check,
	},
	{
		-1,
		-1,
		Q931_NT_ETSI,
		Q931_IE_FACILITY,
		"Facility",
		NULL,
	},
	{
		4,
		2,
		Q931_NT_ETSI,
		Q931_IE_PROGRESS_INDICATOR,
		"Progress Indicator",
		NULL,
	},
	{
		-1,
		4,
		Q931_NT_ETSI,
		Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		"Network Specific Facilities",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ENDPOINT_ID,
		"Endpoint ID",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_NOTIFICATION_INDICATOR,
		"Notification Indicator",
		NULL,
	},
	{
		34,
		1,
		Q931_NT_ETSI,
		Q931_IE_DISPLAY,
		"Display",
		NULL,
	},
	{
		8,
		1,
		Q931_NT_ETSI,
		Q931_IE_DATE_TIME,
		"Date Time",
		NULL,
	},
	{
		34,
		1,
		Q931_NT_ETSI,
		Q931_IE_KEYPAD_FACILITY,
		"Keypad Facility",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CALL_STATUS,
		"Call Status",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_UPDATE,
		"Update",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_INFO_REQUEST,
		"Info Request",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_SIGNAL,
		"Signal",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_SWITCHHOOK,
		"Switchhook",
		NULL,
	},
	{
		4,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_FEATURE_ACTIVATION,
		"Feature Activation",
		NULL,
	},
	{
		5,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_FEATURE_INDICATION,
		"Feature Indication",
		NULL,
	},
	{
		6,
		1,
		Q931_NT_ETSI,
		Q931_IE_INFORMATION_RATE,
		"Information Rate",
		NULL,
	},
	{
		11,
		1,
		Q931_NT_ETSI,
		Q931_IE_END_TO_END_TRANSIT_DELAY,
		"End-to-End Transit Delay",
		NULL,
	},
	{
		5,
		1,
		Q931_NT_ETSI,
		Q931_IE_TRANSIT_DELAY_SELECTION_AND_INDICATION,
		"Transit Delay Selection And Indication",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_LAYER_BINARY_PARAMETERS,
		"Packet Layer Binary Parameters",
		NULL,
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_LAYER_WINDOW_SIZE,
		"Packet Layer Window Size",
		NULL,
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_SIZE,
		"Packet Size",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CLOSED_USER_GROUP,
		"Closed User Group",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REVERSE_CHARGE_INDICATION,
		"Reverse Charge Indication",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CONNECTED_NUMBER,
		"Connected Number",
		NULL,
	},
	{
		24,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLING_PARTY_NUMBER,
		"Calling Party Number",
		NULL,
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLING_PARTY_SUBADDRESS,
		"Calling Party Subaddress",
		NULL,
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLED_PARTY_NUMBER,
		"Called Party Number",
		NULL,
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLED_PARTY_SUBADDRESS,
		"Called Party Subaddress",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ORIGINAL_CALLED_NUMBER,
		"Original Called Number",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_REDIRECTING_NUMBER,
		"Redirecting Number",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTING_SUBADDRESS,
		"Redirecting Subaddress",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTION_NUMBER,
		"Redirection Number",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTION_SUBADDRESS,
		"Redirection Subaddress",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_TRANSIT_NETWORK_SELECTION,
		"Transit Network Selection",
		NULL,
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_RESTART_INDICATOR,
		"Restart Indicator",
		NULL,
	},
	{
		-1,
		-1,
		Q931_NT_UNKNOWN,
		Q931_IE_USER_USER_FACILITY,
		"User User Facility",
		NULL,
	},
	{
		16,
		1,
		Q931_NT_ETSI,
		Q931_IE_LOW_LAYER_COMPATIBILITY,
		"Low Layer Compatibility",
		NULL,
	},
	{
		5,
		1,
		Q931_NT_ETSI,
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		"High Layer Compatibility",
		NULL,
	},
	{
		131,
		1,
		Q931_NT_ETSI,
		Q931_IE_USER_USER,
		"User User",
		NULL,
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ESCAPE_FOR_EXTENSION,
		"Escape For Extension",
		NULL,
	},
};

static struct q931_ie_info_per_mt q931_ie_infos_per_mt[] =
{
	// ALERTING
	{
		Q931_MT_ALERTING,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CALL PROCEEDING
	{
		Q931_MT_CALL_PROCEEDING,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// CONNECT
	{
		Q931_MT_CONNECT,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_DATE_TIME,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_LOW_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CONGESTION CONTROL
	{
		Q931_MT_CONGESTION_CONTROL,
		Q931_IE_CONGESTION_LEVEL,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_CONGESTION_CONTROL,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONGESTION_CONTROL,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// CONNECT ACKNOWLEDGE
	{
		Q931_MT_CONNECT_ACKNOWLEDGE,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT_ACKNOWLEDGE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// DISCONNECT
	{
		Q931_MT_DISCONNECT,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_DISCONNECT,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// FACILITY
	// Defined in unfindable T/S 46-32B

	// INFORMATION
	{
		Q931_MT_INFORMATION,
		Q931_IE_SENDING_COMPLETE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		Q931_IE_KEYPAD_FACILITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		Q931_IE_CALLED_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// NOTIFY
	{
		Q931_MT_NOTIFY,
		Q931_IE_NOTIFICATION_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_NOTIFY,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// PROGRESS
	{
		Q931_MT_PROGRESS,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_PROGRESS,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		Q931_IE_USER_USER,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RELEASE
	{
		Q931_MT_RELEASE,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// RELEASE COMPLETE
	{
		Q931_MT_RELEASE_COMPLETE,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		Q931_IE_USER_USER,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// RESUME
	{
		Q931_MT_RESUME,
		Q931_IE_CALL_IDENTITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// RESUME ACKNOWLEDGE
	{
		Q931_MT_RESUME_ACKNOWLEDGE,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_RESUME_ACKNOWLEDGE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RESUME REJECT
	{
		Q931_MT_RESUME_REJECT,
		Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_RESUME_REJECT,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SETUP
	{
		Q931_MT_SETUP,
		Q931_IE_SENDING_COMPLETE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_KEYPAD_FACILITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_CALLING_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_CALLING_PARTY_SUBADDRESS,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_CALLED_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_CALLED_PARTY_SUBADDRESS,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_TRANSIT_NETWORK_SELECTION,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_LOW_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// SETUP ACKNOWLEDGE
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// STATUS
	{
		Q931_MT_STATUS,
		Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_STATUS,
		Q931_IE_CALL_STATE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_STATUS,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// STATUS ENQUIRY
	{
		Q931_MT_STATUS_ENQUIRY,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND
	{
		Q931_MT_SUSPEND,
		Q931_IE_CALL_IDENTITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND ACKNOWLEDGE
	{
		Q931_MT_SUSPEND_ACKNOWLEDGE,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND REJECT
	{
		Q931_MT_SUSPEND_REJECT,
		Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SUSPEND_REJECT,
		Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// USER INFORMATION
	{
		Q931_MT_USER_INFORMATION,
		Q931_IE_MORE_DATA,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_USER_INFORMATION,
		Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
};

static int q931_ie_id_compare(const void *a, const void *b)
{
	return q931_intcmp(((struct q931_ie_info *)a)->id,
			   ((struct q931_ie_info *)b)->id);
}

static int q931_ie_mt_id_compare(const void *a, const void *b)
{
	if (((struct q931_ie_info_per_mt *)a)->message_type ==
	     ((struct q931_ie_info_per_mt *)b)->message_type)
		return q931_intcmp(((struct q931_ie_info_per_mt *)a)->ie_id,
                	    ((struct q931_ie_info_per_mt *)b)->ie_id);
	else
		return q931_intcmp(((struct q931_ie_info_per_mt *)a)->message_type,
				((struct q931_ie_info_per_mt *)b)->message_type);
}

const struct q931_ie_info *q931_get_ie_info(
	enum q931_ie_id id)
{
	struct q931_ie_info key, *res;

	key.id = id;
	key.name = NULL;

	res = (struct q931_ie_info *)
		bsearch(&key,
			q931_ie_infos,
			sizeof(q931_ie_infos)/
				sizeof(struct q931_ie_info),
			sizeof(struct q931_ie_info),
			q931_ie_id_compare);

	return res;
}

const struct q931_ie_info_per_mt *q931_get_ie_info_per_mt(
	enum q931_message_type message_type,
	enum q931_ie_id ie_id)
{
	struct q931_ie_info_per_mt key, *res;

	key.message_type = message_type;
	key.ie_id = ie_id;

	res = (struct q931_ie_info_per_mt *)
		bsearch(&key,
			q931_ie_infos_per_mt,
			sizeof(q931_ie_infos_per_mt)/
				sizeof(struct q931_ie_info_per_mt),
			sizeof(struct q931_ie_info_per_mt),
			q931_ie_mt_id_compare);

	return res;
}

void q931_ie_infos_init()
{
	qsort(q931_ie_infos,
		sizeof(q931_ie_infos)/
			sizeof(struct q931_ie_info),
		sizeof(struct q931_ie_info),
		q931_ie_id_compare);
	
	qsort(q931_ie_infos_per_mt,
		sizeof(q931_ie_infos_per_mt)/
			sizeof(struct q931_ie_info_per_mt),
		sizeof(struct q931_ie_info_per_mt),
		q931_ie_mt_id_compare);
	
	q931_ie_cause_value_infos_init();
}

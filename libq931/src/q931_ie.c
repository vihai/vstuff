#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <linux/types.h>

#include "q931.h"
#include "q931_ie.h"

static struct q931_ie_info q931_ie_infos[] =
 {
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_SHIFT,
		"Shift"
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_CONGESTION_LEVEL,
		"Congestion Level"
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_MORE_DATA,
		"More Data"
	},
	{
		0,
		1,
		Q931_NT_ETSI,
		Q931_IE_SENDING_COMPLETE,
		"Sending Complete"
	},
	{
		1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REPEAT_INDICATOR,
		"Repeat Indicator"
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_SEGMENTED_MESSAGE,
		"Segmented Message"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CHANGE_STATUS,
		"Change Status"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_SPECIAL,
		"Special"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CONNECTED_ADDRESS,
		"Connected Address"
	},
	{
		13,
		1,
		Q931_NT_ETSI,
		Q931_IE_BEARER_CAPABILITY,
		"Bearer Capability"
	},
	{
		32,
		1,
		Q931_NT_ETSI,
		Q931_IE_CAUSE,
		"Cause"
	},
	{
		10,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALL_IDENTITY,
		"Call Identity"
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALL_STATE,
		"Call State"
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_CHANNEL_IDENTIFICATION,
		"Channel Identification"
	},
	{
		-1,
		-1,
		Q931_NT_ETSI,
		Q931_IE_FACILITY,
		"Facility"
	},
	{
		4,
		2,
		Q931_NT_ETSI,
		Q931_IE_PROGRESS_INDICATOR,
		"Progress Indicator"
	},
	{
		-1,
		4,
		Q931_NT_ETSI,
		Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		"Network Specific Facilities"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ENDPOINT_ID,
		"Endpoint ID"
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_NOTIFICATION_INDICATOR,
		"Notification Indicator"
	},
	{
		34,
		1,
		Q931_NT_ETSI,
		Q931_IE_DISPLAY,
		"Display"
	},
	{
		8,
		1,
		Q931_NT_ETSI,
		Q931_IE_DATE_TIME,
		"Date Time"
	},
	{
		34,
		1,
		Q931_NT_ETSI,
		Q931_IE_KEYPAD_FACILITY,
		"Keypad Facility"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CALL_STATUS,
		"Call Status"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_UPDATE,
		"Update"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_INFO_REQUEST,
		"Info Request"
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_SIGNAL,
		"Signal"
	},
	{
		3,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_SWITCHHOOK,
		"Switchhook"
	},
	{
		4,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_FEATURE_ACTIVATION,
		"Feature Activation"
	},
	{
		5,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_FEATURE_INDICATION,
		"Feature Indication"
	},
	{
		6,
		1,
		Q931_NT_ETSI,
		Q931_IE_INFORMATION_RATE,
		"Information Rate"
	},
	{
		11,
		1,
		Q931_NT_ETSI,
		Q931_IE_END_TO_END_TRANSIT_DELAY,
		"End-to-End Transit Delay"
	},
	{
		5,
		1,
		Q931_NT_ETSI,
		Q931_IE_TRANSIT_DELAY_SELECTION_AND_INDICATION,
		"Transit Delay Selection And Indication"
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_LAYER_BINARY_PARAMETERS,
		"Packet Layer Binary Parameters"
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_LAYER_WINDOW_SIZE,
		"Packet Layer Window Size"
	},
	{
		4,
		1,
		Q931_NT_ETSI,
		Q931_IE_PACKET_SIZE,
		"Packet Size"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CLOSED_USER_GROUP,
		"Closed User Group"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REVERSE_CHARGE_INDICATION,
		"Reverse Charge Indication"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_CONNECTED_NUMBER,
		"Connected Number"
	},
	{
		24,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLING_PARTY_NUMBER,
		"Calling Party Number"
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLING_PARTY_SUBADDRESS,
		"Calling Party Subaddress"
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLED_PARTY_NUMBER,
		"Called Party Number"
	},
	{
		23,
		1,
		Q931_NT_ETSI,
		Q931_IE_CALLED_PARTY_SUBADDRESS,
		"Called Party Subaddress"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ORIGINAL_CALLED_NUMBER,
		"Original Called Number"
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_REDIRECTING_NUMBER,
		"Redirecting Number"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTING_SUBADDRESS,
		"Redirecting Subaddress"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTION_NUMBER,
		"Redirection Number"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_REDIRECTION_SUBADDRESS,
		"Redirection Subaddress"
	},
	{
		-1,
		1,
		Q931_NT_ETSI,
		Q931_IE_TRANSIT_NETWORK_SELECTION,
		"Transit Network Selection"
	},
	{
		3,
		1,
		Q931_NT_ETSI,
		Q931_IE_RESTART_INDICATOR,
		"Restart Indicator"
	},
	{
		-1,
		-1,
		Q931_NT_UNKNOWN,
		Q931_IE_USER_USER_FACILITY,
		"User User Facility"
	},
	{
		16,
		1,
		Q931_NT_ETSI,
		Q931_IE_LOW_LAYER_COMPATIBILITY,
		"Low Layer Compatibility"
	},
	{
		5,
		1,
		Q931_NT_ETSI,
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		"High Layer Compatibility"
	},
	{
		131,
		1,
		Q931_NT_ETSI,
		Q931_IE_USER_USER,
		"User User"
	},
	{
		-1,
		1,
		Q931_NT_UNKNOWN,
		Q931_IE_ESCAPE_FOR_EXTENSION,
		"Escape For Extension"
	},
};

static int q931_ie_id_compare(const void *a, const void *b)
{
 return q931_intcmp(((struct q931_ie_info *)a)->id,
                    ((struct q931_ie_info *)b)->id);
}

const struct q931_ie_info *q931_get_ie_info(int id)
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

void q931_ie_infos_init()
{
 qsort(q931_ie_infos,
       sizeof(q931_ie_infos)/
         sizeof(struct q931_ie_info),
       sizeof(struct q931_ie_info),
       q931_ie_id_compare);

 q931_ie_cause_value_infos_init();
}

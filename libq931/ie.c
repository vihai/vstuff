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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include <libq931/ie.h>
#include <libq931/ie_sending_complete.h>
#include <libq931/ie_bearercap.h>
#include <libq931/ie_cdpn.h>
#include <libq931/ie_cgpn.h>
#include <libq931/ie_chanid.h>
#include <libq931/ie_progind.h>
#include <libq931/ie_cause.h>
#include <libq931/ie_call_state.h>
#include <libq931/ie_hlc.h>
#include <libq931/ie_restind.h>

static struct q931_ie_type q931_ie_types[] =
{
	{
		.max_len	= 0,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_SHIFT,
		.name		= "Shift",
	},
	{
		.max_len	= 0,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CONGESTION_LEVEL,
		.name		= "Congestion Level",
	},
	{
		.max_len	= 0,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_MORE_DATA,
		.name		= "More Data",
	},
	{
		.max_len	= 0,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_SENDING_COMPLETE,
		.name		= "Sending Complete",
		.init		= q931_ie_sending_complete_register,
		.alloc		= q931_ie_sending_complete_alloc_abstract,
		.read_from_buf	= q931_ie_sending_complete_read_from_buf,
		.write_to_buf   = q931_ie_sending_complete_write_to_buf,
		.dump		= q931_ie_sending_complete_dump,
	},
	{
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_REPEAT_INDICATOR,
		.name		= "Repeat Indicator",
	},
	{
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_SEGMENTED_MESSAGE,
		.name		= "Segmented Message",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_CHANGE_STATUS,
		.name		= "Change Status",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_SPECIAL,
		.name		= "Special",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_CONNECTED_ADDRESS,
		.name		= "Connected Address",
	},
	{
		.max_len	= 12,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_BEARER_CAPABILITY,
		.name		= "Bearer Capability",
		.init		= q931_ie_bearer_capability_register,
		.alloc		= q931_ie_bearer_capability_alloc_abstract,
		.read_from_buf	= q931_ie_bearer_capability_read_from_buf,
		.write_to_buf   = q931_ie_bearer_capability_write_to_buf,
		.dump		= q931_ie_bearer_capability_dump,
	},
	{
		.max_len	= 32,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CAUSE,
		.name		= "Cause",
		.init		= q931_ie_cause_register,
		.alloc		= q931_ie_cause_alloc_abstract,
		.read_from_buf	= q931_ie_cause_read_from_buf,
		.write_to_buf   = q931_ie_cause_write_to_buf,
		.dump		= q931_ie_cause_dump,
	},
	{
		.max_len	= 10,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALL_IDENTITY,
		.name		= "Call Identity",
//		.init		= q931_ie_call_identity_register,
//		.alloc		= q931_ie_call_identity_alloc_abstract,
//		.read_from_buf	= q931_ie_call_identity_read_from_buf,
//		.write_to_buf   = q931_ie_call_identity_write_to_buf,
//		.dump		= q931_ie_call_identity_dump,
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALL_STATE,
		.name		= "Call State",
		.init		= q931_ie_call_state_register,
		.alloc		= q931_ie_call_state_alloc_abstract,
		.read_from_buf	= q931_ie_call_state_read_from_buf,
		.write_to_buf   = q931_ie_call_state_write_to_buf,
		.dump		= q931_ie_call_state_dump,
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CHANNEL_IDENTIFICATION,
		.name		= "Channel Identification",
		.init		= q931_ie_channel_identification_register,
		.alloc		= q931_ie_channel_identification_alloc_abstract,
		.read_from_buf	= q931_ie_channel_identification_read_from_buf,
		.write_to_buf   = q931_ie_channel_identification_write_to_buf,
		.dump		= q931_ie_channel_identification_dump,
	},
	{
		.max_len	= -1,
		.max_occur	= -1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_FACILITY,
		.name		= "Facility",
	},
	{
		.max_len	= 4,
		.max_occur	= 2,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_PROGRESS_INDICATOR,
		.name		= "Progress Indicator",
		.init		= q931_ie_progress_indicator_register,
		.alloc		= q931_ie_progress_indicator_alloc_abstract,
		.read_from_buf	= q931_ie_progress_indicator_read_from_buf,
		.write_to_buf   = q931_ie_progress_indicator_write_to_buf,
		.dump		= q931_ie_progress_indicator_dump,
	},
	{
		.max_len	= -1,
		.max_occur	= 4,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		.name		= "Network Specific Facilities",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_ENDPOINT_ID,
		.name		= "Endpoint ID",
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_NOTIFICATION_INDICATOR,
		.name		= "Notification Indicator",
	},
	{
		.max_len	= 82,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_DISPLAY,
		.name		= "Display",
	},
	{
		.max_len	= 8,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_DATE_TIME,
		.name		= "Date Time",
	},
	{
		.max_len	= 34,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_KEYPAD_FACILITY,
		.name		= "Keypad Facility",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_CALL_STATUS,
		.name		= "Call Status",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_UPDATE,
		.name		= "Update",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_INFO_REQUEST,
		.name		= "Info Request",
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_SIGNAL,
		.name		= "Signal",
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_SWITCHHOOK,
		.name		= "Switchhook",
	},
	{
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_FEATURE_ACTIVATION,
		.name		= "Feature Activation",
	},
	{
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_FEATURE_INDICATION,
		.name		= "Feature Indication",
	},
	{
		.max_len	= 6,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_INFORMATION_RATE,
		.name		= "Information Rate",
	},
	{
		.max_len	= 11,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_END_TO_END_TRANSIT_DELAY,
		.name		= "End-to-End Transit Delay",
	},
	{
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_TRANSIT_DELAY_SELECTION_AND_INDICATION,
		.name		= "Transit Delay Selection And Indication",
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_PACKET_LAYER_BINARY_PARAMETERS,
		.name		= "Packet Layer Binary Parameters",
	},
	{
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_PACKET_LAYER_WINDOW_SIZE,
		.name		= "Packet Layer Window Size",
	},
	{
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_PACKET_SIZE,
		.name		= "Packet Size",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_CLOSED_USER_GROUP,
		.name		= "Closed User Group",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_REVERSE_CHARGE_INDICATION,
		.name		= "Reverse Charge Indication",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_CONNECTED_NUMBER,
		.name		= "Connected Number",
	},
	{
		.max_len	= 24,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALLING_PARTY_NUMBER,
		.name		= "Calling Party Number",
		.init		= q931_ie_calling_party_number_register,
		.alloc		= q931_ie_calling_party_number_alloc_abstract,
		.read_from_buf	= q931_ie_calling_party_number_read_from_buf,
		.write_to_buf   = q931_ie_calling_party_number_write_to_buf,
		.dump		= q931_ie_calling_party_number_dump,
	},
	{
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALLING_PARTY_SUBADDRESS,
		.name		= "Calling Party Subaddress",
	},
	{
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALLED_PARTY_NUMBER,
		.name		= "Called Party Number",
		.init		= q931_ie_called_party_number_register,
		.alloc		= q931_ie_called_party_number_alloc_abstract,
		.read_from_buf	= q931_ie_called_party_number_read_from_buf,
		.write_to_buf   = q931_ie_called_party_number_write_to_buf,
		.dump		= q931_ie_called_party_number_dump,
	},
	{
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_CALLED_PARTY_SUBADDRESS,
		.name		= "Called Party Subaddress",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_ORIGINAL_CALLED_NUMBER,
		.name		= "Original Called Number",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_REDIRECTING_NUMBER,
		.name		= "Redirecting Number",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_REDIRECTING_SUBADDRESS,
		.name		= "Redirecting Subaddress",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_REDIRECTION_NUMBER,
		.name		= "Redirection Number",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_REDIRECTION_SUBADDRESS,
		.name		= "Redirection Subaddress",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_TRANSIT_NETWORK_SELECTION,
		.name		= "Transit Network Selection",
	},
	{
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_RESTART_INDICATOR,
		.name		= "Restart Indicator",
		.init		= q931_ie_restart_indicator_register,
		.alloc		= q931_ie_restart_indicator_alloc_abstract,
		.read_from_buf	= q931_ie_restart_indicator_read_from_buf,
		.write_to_buf   = q931_ie_restart_indicator_write_to_buf,
		.dump		= q931_ie_restart_indicator_dump,
	},
	{
		.max_len	= -1,
		.max_occur	= -1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_USER_USER_FACILITY,
		.name		= "User User Facility",
	},
	{
		.max_len	= 16,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_LOW_LAYER_COMPATIBILITY,
		.name		= "Low Layer Compatibility",
	},
	{
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_HIGH_LAYER_COMPATIBILITY,
		.name		= "High Layer Compatibility",
	},
	{
		.max_len	= 131,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.id		= Q931_IE_USER_USER,
		.name		= "User User",
	},
	{
		.max_len	= -1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.id		= Q931_IE_ESCAPE_FOR_EXTENSION,
		.name		= "Escape For Extension",
	},
};

static struct q931_ie_type_per_mt q931_ie_types_per_mt[] =
{
	// ALERTING
	{
		Q931_MT_ALERTING,
		Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		Q931_IE_FACILITY, //???
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
		Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CALL PROCEEDING
	{
		Q931_MT_CALL_PROCEEDING,
		Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
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
	{
		Q931_MT_CALL_PROCEEDING,
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CONNECT
	{
		Q931_MT_CONNECT,
		Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_FACILITY, //???
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
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{ //??? Probably right, see ETS 300 097
		Q931_MT_CONNECT,
		Q931_IE_CONNECTED_NUMBER,
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
		Q931_IE_FACILITY, //???
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
		Q931_IE_USER_USER, //???
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
		Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
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
		Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		Q931_IE_USER_USER, //???
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
		Q931_IE_FACILITY, //???
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
		Q931_IE_USER_USER, //???
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
		Q931_IE_FACILITY, //????
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
		Q931_IE_USER_USER, //???
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
		Q931_IE_FACILITY, //???
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
		Q931_IE_DATE_TIME,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		Q931_IE_USER_USER, //???
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
	return q931_intcmp(((struct q931_ie_type *)a)->id,
			   ((struct q931_ie_type *)b)->id);
}

static int q931_ie_mt_id_compare(const void *a, const void *b)
{
	if (((struct q931_ie_type_per_mt *)a)->message_type ==
	     ((struct q931_ie_type_per_mt *)b)->message_type)
		return q931_intcmp(((struct q931_ie_type_per_mt *)a)->ie_id,
                	    ((struct q931_ie_type_per_mt *)b)->ie_id);
	else
		return q931_intcmp(((struct q931_ie_type_per_mt *)a)->message_type,
				((struct q931_ie_type_per_mt *)b)->message_type);
}

const struct q931_ie_type *q931_get_ie_type(
	enum q931_ie_id id)
{
	struct q931_ie_type key, *res;

	key.id = id;
	key.name = NULL;

	res = (struct q931_ie_type *)
		bsearch(&key,
			q931_ie_types,
			sizeof(q931_ie_types)/
				sizeof(struct q931_ie_type),
			sizeof(struct q931_ie_type),
			q931_ie_id_compare);

	return res;
}

const struct q931_ie_type_per_mt *q931_get_ie_type_per_mt(
	enum q931_message_type message_type,
	enum q931_ie_id ie_id)
{
	struct q931_ie_type_per_mt key, *res;

	key.message_type = message_type;
	key.ie_id = ie_id;

	res = (struct q931_ie_type_per_mt *)
		bsearch(&key,
			q931_ie_types_per_mt,
			sizeof(q931_ie_types_per_mt)/
				sizeof(struct q931_ie_type_per_mt),
			sizeof(struct q931_ie_type_per_mt),
			q931_ie_mt_id_compare);

	return res;
}

void q931_ie_types_init()
{
	qsort(q931_ie_types,
		sizeof(q931_ie_types)/
			sizeof(struct q931_ie_type),
		sizeof(struct q931_ie_type),
		q931_ie_id_compare);

	qsort(q931_ie_types_per_mt,
		sizeof(q931_ie_types_per_mt)/
			sizeof(struct q931_ie_type_per_mt),
		sizeof(struct q931_ie_type_per_mt),
		q931_ie_mt_id_compare);

	int i;
	for (i=0;
	     i<sizeof(q931_ie_types)/
		sizeof(struct q931_ie_type); i++) {

		if (q931_ie_types[i].init)
			q931_ie_types[i].init(&q931_ie_types[i]);
	}
}

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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <linux/types.h>

#define Q931_PRIVATE

#include <libq931/ie.h>
#include <libq931/ie_bearer_capability.h>
#include <libq931/ie_call_identity.h>
#include <libq931/ie_call_state.h>
#include <libq931/ie_called_party_number.h>
#include <libq931/ie_connected_number.h>
#include <libq931/ie_calling_party_number.h>
#include <libq931/ie_cause.h>
#include <libq931/ie_channel_identification.h>
#include <libq931/ie_display.h>
#include <libq931/ie_datetime.h>
#include <libq931/ie_low_layer_compatibility.h>
#include <libq931/ie_high_layer_compatibility.h>
#include <libq931/ie_notification_indicator.h>
#include <libq931/ie_progress_indicator.h>
#include <libq931/ie_restart_indicator.h>
#include <libq931/ie_sending_complete.h>

static struct q931_ie_class q931_ie_classes[] =
{
	{
		.type		= Q931_IE_TYPE_SO,
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_SHIFT,
		.name		= "Shift",
	},
	{
		.type		= Q931_IE_TYPE_SO,
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_MORE_DATA,
		.name		= "More Data",
	},
	{
		.type		= Q931_IE_TYPE_SO,
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_SENDING_COMPLETE,
		.name		= "Sending Complete",
		.init		= q931_ie_sending_complete_register,
		.alloc		= q931_ie_sending_complete_alloc_abstract,
		.read_from_buf	= q931_ie_sending_complete_read_from_buf,
		.write_to_buf   = q931_ie_sending_complete_write_to_buf,
		.dump		= q931_ie_sending_complete_dump,
	},
	{
		.type		= Q931_IE_TYPE_SO,
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CONGESTION_LEVEL,
		.name		= "Congestion Level",
	},
	{
		.type		= Q931_IE_TYPE_SO,
		.max_len	= 1,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_REPEAT_INDICATOR,
		.name		= "Repeat Indicator",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_SEGMENTED_MESSAGE,
		.name		= "Segmented Message",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_CHANGE_STATUS,
		.name		= "Change Status",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_SPECIAL,
		.name		= "Special",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_CONNECTED_ADDRESS,
		.name		= "Connected Address",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= INT_MAX,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_EXTENDED_FACILITY,
		.name		= "Extended Facility",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 12,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_BEARER_CAPABILITY,
		.name		= "Bearer Capability",
		.init		= q931_ie_bearer_capability_register,
		.alloc		= q931_ie_bearer_capability_alloc_abstract,
		.read_from_buf	= q931_ie_bearer_capability_read_from_buf,
		.write_to_buf   = q931_ie_bearer_capability_write_to_buf,
		.dump		= q931_ie_bearer_capability_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 32,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CAUSE,
		.name		= "Cause",
		.init		= q931_ie_cause_register,
		.alloc		= q931_ie_cause_alloc_abstract,
		.read_from_buf	= q931_ie_cause_read_from_buf,
		.write_to_buf   = q931_ie_cause_write_to_buf,
		.dump		= q931_ie_cause_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 10,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALL_IDENTITY,
		.name		= "Call Identity",
		.init		= q931_ie_call_identity_register,
		.alloc		= q931_ie_call_identity_alloc_abstract,
		.read_from_buf	= q931_ie_call_identity_read_from_buf,
		.write_to_buf   = q931_ie_call_identity_write_to_buf,
		.dump		= q931_ie_call_identity_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALL_STATE,
		.name		= "Call State",
		.init		= q931_ie_call_state_register,
		.alloc		= q931_ie_call_state_alloc_abstract,
		.read_from_buf	= q931_ie_call_state_read_from_buf,
		.write_to_buf   = q931_ie_call_state_write_to_buf,
		.dump		= q931_ie_call_state_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= INT_MAX,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CHANNEL_IDENTIFICATION,
		.name		= "Channel Identification",
		.init		= q931_ie_channel_identification_register,
		.alloc		= q931_ie_channel_identification_alloc_abstract,
		.read_from_buf	= q931_ie_channel_identification_read_from_buf,
		.write_to_buf   = q931_ie_channel_identification_write_to_buf,
		.dump		= q931_ie_channel_identification_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= INT_MAX,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_FACILITY,
		.name		= "Facility",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 4,
		.max_occur	= 2,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_PROGRESS_INDICATOR,
		.name		= "Progress Indicator",
		.init		= q931_ie_progress_indicator_register,
		.alloc		= q931_ie_progress_indicator_alloc_abstract,
		.read_from_buf	= q931_ie_progress_indicator_read_from_buf,
		.write_to_buf   = q931_ie_progress_indicator_write_to_buf,
		.dump		= q931_ie_progress_indicator_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 4,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		.name		= "Network Specific Facilities",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_ENDPOINT_ID,
		.name		= "Endpoint ID",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_NOTIFICATION_INDICATOR,
		.name		= "Notification Indicator",
		.init		= q931_ie_notification_indicator_register,
		.alloc		= q931_ie_notification_indicator_alloc_abstract,
		.read_from_buf	= q931_ie_notification_indicator_read_from_buf,
		.write_to_buf   = q931_ie_notification_indicator_write_to_buf,
		.dump		= q931_ie_notification_indicator_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 82,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_DISPLAY,
		.name		= "Display",
		.init		= q931_ie_display_register,
		.alloc		= q931_ie_display_alloc_abstract,
		.read_from_buf	= q931_ie_display_read_from_buf,
		.write_to_buf   = q931_ie_display_write_to_buf,
		.dump		= q931_ie_display_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 8,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_DATETIME,
		.name		= "Date Time",
		.init		= q931_ie_datetime_register,
		.alloc		= q931_ie_datetime_alloc_abstract,
		.read_from_buf	= q931_ie_datetime_read_from_buf,
		.write_to_buf   = q931_ie_datetime_write_to_buf,
		.dump		= q931_ie_datetime_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 34,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_KEYPAD_FACILITY,
		.name		= "Keypad Facility",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_CALL_STATUS,
		.name		= "Call Status",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_UPDATE,
		.name		= "Update",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_INFO_REQUEST,
		.name		= "Info Request",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_SIGNAL,
		.name		= "Signal",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_SWITCHHOOK,
		.name		= "Switchhook",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_FEATURE_ACTIVATION,
		.name		= "Feature Activation",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_FEATURE_INDICATION,
		.name		= "Feature Indication",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 6,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_INFORMATION_RATE,
		.name		= "Information Rate",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 11,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_END_TO_END_TRANSIT_DELAY,
		.name		= "End-to-End Transit Delay",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_TRANSIT_DELAY_SELECTION_AND_INDICATION,
		.name		= "Transit Delay Selection And Indication",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_PACKET_LAYER_BINARY_PARAMETERS,
		.name		= "Packet Layer Binary Parameters",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_PACKET_LAYER_WINDOW_SIZE,
		.name		= "Packet Layer Window Size",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 4,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_PACKET_SIZE,
		.name		= "Packet Size",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_CLOSED_USER_GROUP,
		.name		= "Closed User Group",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_REVERSE_CHARGE_INDICATION,
		.name		= "Reverse Charge Indication",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_CONNECTED_NUMBER,
		.name		= "Connected Number",
		.init		= q931_ie_connected_number_register,
		.alloc		= q931_ie_connected_number_alloc_abstract,
		.read_from_buf	= q931_ie_connected_number_read_from_buf,
		.write_to_buf   = q931_ie_connected_number_write_to_buf,
		.dump		= q931_ie_connected_number_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 24,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALLING_PARTY_NUMBER,
		.name		= "Calling Party Number",
		.init		= q931_ie_calling_party_number_register,
		.alloc		= q931_ie_calling_party_number_alloc_abstract,
		.read_from_buf	= q931_ie_calling_party_number_read_from_buf,
		.write_to_buf   = q931_ie_calling_party_number_write_to_buf,
		.dump		= q931_ie_calling_party_number_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALLING_PARTY_SUBADDRESS,
		.name		= "Calling Party Subaddress",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALLED_PARTY_NUMBER,
		.name		= "Called Party Number",
		.init		= q931_ie_called_party_number_register,
		.alloc		= q931_ie_called_party_number_alloc_abstract,
		.read_from_buf	= q931_ie_called_party_number_read_from_buf,
		.write_to_buf   = q931_ie_called_party_number_write_to_buf,
		.dump		= q931_ie_called_party_number_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 23,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_CALLED_PARTY_SUBADDRESS,
		.name		= "Called Party Subaddress",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_ORIGINAL_CALLED_NUMBER,
		.name		= "Original Called Number",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_REDIRECTING_NUMBER,
		.name		= "Redirecting Number",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_REDIRECTING_SUBADDRESS,
		.name		= "Redirecting Subaddress",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_REDIRECTION_NUMBER,
		.name		= "Redirection Number",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_REDIRECTION_SUBADDRESS,
		.name		= "Redirection Subaddress",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_TRANSIT_NETWORK_SELECTION,
		.name		= "Transit Network Selection",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 3,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_RESTART_INDICATOR,
		.name		= "Restart Indicator",
		.init		= q931_ie_restart_indicator_register,
		.alloc		= q931_ie_restart_indicator_alloc_abstract,
		.read_from_buf	= q931_ie_restart_indicator_read_from_buf,
		.write_to_buf   = q931_ie_restart_indicator_write_to_buf,
		.dump		= q931_ie_restart_indicator_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= INT_MAX,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_USER_USER_FACILITY,
		.name		= "User User Facility",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 16,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_LOW_LAYER_COMPATIBILITY,
		.name		= "Low Layer Compatibility",
		.init		= q931_ie_low_layer_compatibility_register,
		.alloc		= q931_ie_low_layer_compatibility_alloc_abstract,
		.read_from_buf	= q931_ie_low_layer_compatibility_read_from_buf,
		.write_to_buf   = q931_ie_low_layer_compatibility_write_to_buf,
		.dump		= q931_ie_low_layer_compatibility_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 5,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_HIGH_LAYER_COMPATIBILITY,
		.name		= "High Layer Compatibility",
		.init		= q931_ie_high_layer_compatibility_register,
		.alloc		= q931_ie_high_layer_compatibility_alloc_abstract,
		.read_from_buf	= q931_ie_high_layer_compatibility_read_from_buf,
		.write_to_buf   = q931_ie_high_layer_compatibility_write_to_buf,
		.dump		= q931_ie_high_layer_compatibility_dump,
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= 131,
		.max_occur	= 1,
		.network_type	= Q931_NT_ETSI,
		.codeset	= 0,
		.id		= Q931_IE_USER_USER,
		.name		= "User User",
	},
	{
		.type		= Q931_IE_TYPE_VL,
		.max_len	= INT_MAX,
		.max_occur	= 1,
		.network_type	= Q931_NT_UNKNOWN,
		.codeset	= 0,
		.id		= Q931_IE_ESCAPE_FOR_EXTENSION,
		.name		= "Escape For Extension",
	},
};

struct q931_ie_usage q931_ie_usages[] =
{
	// ALERTING
	{
		Q931_MT_ALERTING,
		0, Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_ALERTING,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CALL PROCEEDING
	{
		Q931_MT_CALL_PROCEEDING,
		0, Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CALL_PROCEEDING,
		0, Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CONNECT
	{
		Q931_MT_CONNECT,
		0, Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_DATETIME,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_LOW_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONNECT,
		0, Q931_IE_CONNECTED_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// CONGESTION CONTROL
	{
		Q931_MT_CONGESTION_CONTROL,
		0, Q931_IE_CONGESTION_LEVEL,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_CONGESTION_CONTROL,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_CONGESTION_CONTROL,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// CONNECT ACKNOWLEDGE
	{
		Q931_MT_CONNECT_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// DISCONNECT
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_DISCONNECT,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// FACILITY
	{
		Q931_MT_FACILITY,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_FACILITY,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_FACILITY,
		0, Q931_IE_CALLED_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_FACILITY,
		0, Q931_IE_CALLED_PARTY_SUBADDRESS,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_FACILITY,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// REGISTER
	{
		Q931_MT_REGISTER,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_REGISTER,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_REGISTER,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// INFORMATION
	{
		Q931_MT_INFORMATION,
		0, Q931_IE_SENDING_COMPLETE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		0, Q931_IE_KEYPAD_FACILITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_INFORMATION,
		0, Q931_IE_CALLED_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// NOTIFY
	{
		Q931_MT_NOTIFY,
		0, Q931_IE_NOTIFICATION_INDICATOR,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_NOTIFY,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// HOLD
	{
		Q931_MT_HOLD,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// HOLD ACKNOWLEDGE
	{
		Q931_MT_HOLD_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// HOLD REJECT
	{
		Q931_MT_HOLD_REJECT,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_HOLD_REJECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RETRIEVE
	{
		Q931_MT_RETRIEVE,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RETRIEVE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RETRIEVE ACKNOWLEDGE
	{
		Q931_MT_RETRIEVE_ACKNOWLEDGE,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RETRIEVE_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RETRIEVE_REJECT
	{
		Q931_MT_RETRIEVE_REJECT,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_RETRIEVE_REJECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// PROGRESS
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_PROGRESS,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RELEASE
	{
		Q931_MT_RELEASE,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// RELEASE COMPLETE
	{
		Q931_MT_RELEASE_COMPLETE,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RELEASE_COMPLETE,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// RESTART
	{
		Q931_MT_RESTART,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RESTART,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RESTART,
		0, Q931_IE_RESTART_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},

	// RESTART ACKNOWLEDGE
	{
		Q931_MT_RESTART_ACKNOWLEDGE,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RESTART_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_RESTART_ACKNOWLEDGE,
		0, Q931_IE_RESTART_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},

	// RESUME
	{
		Q931_MT_RESUME,
		0, Q931_IE_CALL_IDENTITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// RESUME ACKNOWLEDGE
	{
		Q931_MT_RESUME_ACKNOWLEDGE,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_RESUME_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// RESUME REJECT
	{
		Q931_MT_RESUME_REJECT,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_RESUME_REJECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SETUP
	{
		Q931_MT_SETUP,
		0, Q931_IE_SENDING_COMPLETE,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_BEARER_CAPABILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_EXTENDED_FACILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_NETWORK_SPECIFIC_FACILITIES,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_KEYPAD_FACILITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_CALLING_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_CALLING_PARTY_SUBADDRESS,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_CALLED_PARTY_NUMBER,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_CALLED_PARTY_SUBADDRESS,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_TRANSIT_NETWORK_SELECTION,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_LOW_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_HIGH_LAYER_COMPATIBILITY,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_DATETIME,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP,
		0, Q931_IE_USER_USER, //???
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},

	// SETUP ACKNOWLEDGE
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		0, Q931_IE_CHANNEL_IDENTIFICATION,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		0, Q931_IE_PROGRESS_INDICATOR,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_SETUP_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// STATUS
	{
		Q931_MT_STATUS,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_STATUS,
		0, Q931_IE_CALL_STATE,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_STATUS,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// STATUS ENQUIRY
	{
		Q931_MT_STATUS_ENQUIRY,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND
	{
		Q931_MT_SUSPEND,
		0, Q931_IE_CALL_IDENTITY,
		Q931_IE_DIR_U_TO_N,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND ACKNOWLEDGE
	{
		Q931_MT_SUSPEND_ACKNOWLEDGE,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// SUSPEND REJECT
	{
		Q931_MT_SUSPEND_REJECT,
		0, Q931_IE_CAUSE,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_MANDATORY,
	},
	{
		Q931_MT_SUSPEND_REJECT,
		0, Q931_IE_DISPLAY,
		Q931_IE_DIR_N_TO_U,
		Q931_IE_OPTIONAL,
	},

	// USER INFORMATION
	{
		Q931_MT_USER_INFORMATION,
		0, Q931_IE_MORE_DATA,
		Q931_IE_DIR_BOTH,
		Q931_IE_OPTIONAL,
	},
	{
		Q931_MT_USER_INFORMATION,
		0, Q931_IE_USER_USER,
		Q931_IE_DIR_BOTH,
		Q931_IE_MANDATORY,
	},
};

int q931_ie_usages_cnt = ARRAY_SIZE(q931_ie_usages);

const struct q931_ie_class *q931_get_ie_class(
	__u8 codeset,
	enum q931_ie_id id)
{
	int i;
	for (i=0; i<ARRAY_SIZE(q931_ie_classes); i++) {
		if(q931_ie_classes[i].codeset == codeset &&
		   q931_ie_classes[i].id == id)
			return &q931_ie_classes[i];
	}

	return NULL;
}

const struct q931_ie_usage *q931_get_ie_usage(
	enum q931_message_type message_type,
	__u8 codeset,
	enum q931_ie_id ie_id)
{
	int i;
	for (i=0; i<ARRAY_SIZE(q931_ie_usages); i++) {
		if(q931_ie_usages[i].codeset == codeset &&
		   q931_ie_usages[i].ie_id == ie_id)
			return &q931_ie_usages[i];
	}

	return NULL;
}

void q931_ie_classes_init()
{
	int i;
	for (i=0; i<ARRAY_SIZE(q931_ie_classes); i++) {

		if (q931_ie_classes[i].init)
			q931_ie_classes[i].init(&q931_ie_classes[i]);
	}
}

struct q931_ie *q931_ie_get(struct q931_ie *ie)
{
	assert(ie);
	assert(ie->refcnt > 0);

	ie->refcnt++;

	return ie;
}

void _q931_ie_put(struct q931_ie *ie)
{
	assert(ie);
	assert(ie->refcnt > 0);

	ie->refcnt--;

	if (ie->refcnt == 0)
		free(ie);
}


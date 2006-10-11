/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stddef.h>

#include <asterisk/causes.h>

#include "causes.h"

static const char *vgsm_cause_reason_2_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "Normal event";
	case 1:
		return "Abnormal release, unspecified";
	case 2:
		return "Abnormal release, channel unacceptable";
	case 3:
		return "Abnormal release, timer expired";
	case 4:
		return "Abnormal release, no activity on the radio path";
	case 5:
		return "Pre-emptive release";
	case 8:
		return "Handover impossible, timing advance out of range";
	case 9:
		return "Channel mode unacceptable";
	case 10:
		return "Frequency not implemented";
	case 65:
		return "Call already cleared";
	case 95:
		return "Semantically incorrect message";
	case 96:
		return "Invalid mandatory information";
	case 97:
		return "Message type non-existent or not implemented";
	case 98:
		return "Message type not compatible with protocol state";
	case 100:
		return "Conditional information element error";
	case 101:
		return "No cell allocation available";
	case 111:
		return "Protocol error unspecified";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_3_to_text(int cause)
{
	switch(cause) {
	case 1:
		return "Racchs not answered";
	case 2:
		return "Racchs rejected";
	case 3:
		return "Access class of the SIM is barred by the network"
			" provider";
	case 4:
		return "SABM failure";
	case 5:
		return "Radio link counter expiry or PerformAbnormalRelease";
	case 6:
		return "Confirm ABORT of the MM";
	case 7:
		return "Respond to DEACT REQ";
	case 8:
		return "Loss of coverage";
	case 9:
		return "Reestablishment not possible";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_4_6_50_to_text(int cause)
{
	switch(cause) {
	case 2:
		return "IMSI unknown in HLR";
	case 3:
		return "Illegal MS";
	case 4:
		return "IMSI unknown in VLR";
	case 5:
		return "IMEI not accepted";
	case 6:
		return "Illegal ME";
	case 11:
		return "PLMN not allowed";
	case 12:
		return "Location Area not allowed";
	case 13:
		return "Roaming not allowed in this location area";
	case 17:
		return "Network failure";
	case 22:
		return "Congestion";
	case 32:
		return "Service option not supported";
	case 33:
		return "Requested service option not subscribed";
	case 34:
		return "Service option temporarily out of order";
	case 36:
		return "Regular PDP context deactivation";
	case 38:
		return "Call cannot be identified";
	case 95:
		return "Semantically incorrect message";
	case 96:
		return "Invalid mandatory information";
	case 97:
		return "Message type non-existent or not implemented";
	case 98:
		return "Message not compatible with protocol state";
	case 99:
		return "Information element non-existent or not implemented";
	case 100:
		return "Conditional information element error";
	case 101:
		return "Messages not compatible with protocol state";
	case 111:
		return "Protocol error, unspecified";
	case 7:
		return "GPRS services not allowed";
	case 8:
		return "GPRS services not allowed in combination with non-GPRS"
			" services";
	case 9:
		return "MS identity cannot be identified by the network";
	case 10:
		return "Implicitly detached";
	case 14:
		return "GPRS services not allowed in current PLMN";
	case 16:
		return "MSC temporarily unreachable";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_5_7_to_text(int cause)
{
	switch(cause) {
	case 1:
		return "No SIM available";
	case 8:
		return "No MM connection";
	case 9:
		return "Authentification failure";
	case 11:
		return "MM performs detach";
	case 17:
		return "Registration failed and will be re-attempted in a"
			" short term";
	case 18:
		return "CM connection establishment failed";
	case 19:
		return "Registration failed and will be re-attempted in a long"
			" term";
	case 20:
		return "RR connection is released";
	case 21:
		return "MS tries to register";
	case 22:
		return "SPLMN is not available";
	case 23:
		return "An MTC is in progress";
	case 24:
		return "A PLMN scan is in progress";
	case 25:
		return "The MM is detached, the MS is in MS class C GPRS only";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_8_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "No error";
	case 1:
		return "Unassigned (unallocated) number";
	case 3:
		return "No route to destination";
	case 6:
		return "Channel unacceptable";
	case 8:
		return "Operator determined barring";
	case 16:
		return "Normal call clearing";
	case 17:
		return "User busy";
	case 18:
		return "No user responding";
	case 19:
		return "User alerting, no answer";
	case 21:
		return "Call rejected";
	case 22:
		return "Number changed";
	case 25:
		return "Pre-emption";
	case 26:
		return "Non-selected user clearing";
	case 27:
		return "Destination out of order";
	case 28:
		return "Invalid number format (incomplete number)";
	case 29:
		return "Facility rejected";
	case 30:
		return "Response to STATUS ENQUIRY";
	case 31:
		return "Normal, unspecified";
	case 34:
		return "No circuit/channel available";
	case 38:
		return "Network out of order";
	case 41:
		return "Temporary failure";
	case 42:
		return "Switching equipment congestion";
	case 43:
		return "Access information discarded";
	case 44:
		return "Requested circuit/channel not available";
	case 47:
		return "Resource unavailable, unspecified";
	case 49:
		return "Quality of service unavailable";
	case 50:
		return "Requested facility not subscribed";
	case 55:
		return "Incoming calls barred within the CUG";
	case 57:
		return "Bearer capability not authorized";
	case 58:
		return "Bearer capability not presently available";
	case 63:
		return "Service or option not available, unspecified";
	case 65:
		return "Bearer service not implemented";
	case 68:
		return "ACM equal or greater than ACMmax";
	case 69:
		return "Requested facility not implemented";
	case 70:
		return "Only restricted digital information bearer capability"
			" is available";
	case 79:
		return "service or option not implemented, unspecified";
	case 81:
		return "Invalid transaction identifier value";
	case 87:
		return "User not member of CUG";
	case 88:
		return "Incompatible destination";
	case 91:
		return "Invalid transit network selection";
	case 95:
		return "Semantically incorrect message";
	case 96:
		return "Invalid mandatory information";
	case 97:
		return "Message type non-existant or not implemented";
	case 98:
		return "Message type not comaptible with protocol state";
	case 99:
		return "Information element non-existent or not implemented";
	case 100:
		return "Conditional information element error";
	case 101:
		return "Message not compatible with protocol";
	case 102:
		return "Recovery on timer expiry";
	case 111:
		return "Protocol error, unspecified";
	case 127:
		return "Interworking, unspecified";
	case 1000:
		return "Local";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_9_to_text(int cause)
{
	switch(cause) {
	case 1:
		return "Call dropped";
	case 2:
		return "Service not available";
	case 3:
		return "Hold procedure not available";
	case 4:
		return "Temporary no service, previous procedure not yet"
			" finished";
	case 5:
		return "No speech service available";
	case 6:
		return "Call reestablishment procedure active";
	case 7:
		return "Mobile received a release (complete) message during a"
			" modify procedure (modify reject)";
	case 8:
		return "Call clearing, because loss of radio connection, if no"
			" reestablishment is allowed (call not active)";
	case 10:
		return "Number not included in FDN list";
	case 300:
		return "Called party barred incoming call";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_11_to_text(int cause)
{
	switch(cause) {
	case 1:
		return "SIM data not available";
	case 2:
		return "SIM does not support AOC";
	case 3:
		return "SIM data access error";
	case 4:
		return "ACM limit almost reached ACM range overflow";
	case 5:
		return "ACM range overflow";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_21_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "No error (default)";
	case 1:
		return "UnknownSubscriber";
	case 9:
		return "IllegalSubscriber";
	case 10:
		return "BearerServiceNotProvisioned";
	case 11:
		return "TeleserviceNotProvisioned";
	case 12:
		return "IllegalEquipment";
	case 13:
		return "CallBarred";
	case 15:
		return "CUGReject";
	case 16:
		return "IllegalSSOperation";
	case 17:
		return "SSErrorStatus";
	case 18:
		return "SSNotAvailable";
	case 19:
		return "SSSubscriptionViolation";
	case 20:
		return "SSIncompatibility";
	case 21:
		return "FacilityNotSupported";
	case 27:
		return "AbsentSubscriber";
	case 29:
		return "ShortTermDenial";
	case 30:
		return "LongTermDenial";
	case 34:
		return "SystemFailure";
	case 35:
		return "DataMissing";
	case 36:
		return "UnexpectedDataValue";
	case 37:
		return "PWRegistrationFailure";
	case 38:
		return "NegativePWCheck";
	case 43:
		return "NumberOfPWAttemptsViolation";
	case 71:
		return "UnknownAlphabet";
	case 72:
		return "USSDBusy";
	case 126:
		return "MaxNumsOfMPTYCallsExceeded";
	case 127:
		return "ResourcesNotAvailable";
	case 300:
		return "Unrecognized Component";
	case 301:
		return "Mistyped Component";
	case 302:
		return "Badly Structured Component";
	case 303:
		return "Duplicate Invoke ID";
	case 304:
		return "Unrecognized Operation";
	case 305:
		return "Mistyped Parameter";
	case 306:
		return "Resource Limitation";
	case 307:
		return "Initiating Release";
	case 308:
		return "Unrecognized Linked ID";
	case 309:
		return "Linked Response Unexpected";
	case 310:
		return "Unexpected Linked Operation";
	case 311:
		return "Unrecognize Invoke ID";
	case 312:
		return "Return Result Unexpected";
	case 313:
		return "Mistyped Parameter";
	case 314:
		return "Unrecognized Invoke ID";
	case 315:
		return "Return Error Unexpected";
	case 316:
		return "Unrecognized Error";
	case 317:
		return "Unexpected Error";
	case 318:
		return "Mistyped Parameter";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_22_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "ECT procedure failed (timer expired)";
	case 1:
		return "Call has been cleared without receiving an answer to"
			" ECT request";
	case 2:
		return "Initial conditions not fulfilled (one active, one held"
			" call)";
	case 3:
		return "Received \"return error\"";
	case 4:
		return "Call has been cleared without receiving an answer to"
			" CCBS request";
	case 5:
		return "Initial conditions for CCBS not fulfilled (Idle CRSS)";
	case 25:
		return "LLC or SNDCP failure";
	case 26:
		return "Insufficient resources";
	case 27:
		return "Unknown or missing access point name";
	case 28:
		return "Unknown PDP address or PDP type";
	case 29:
		return "User authentification failed";
	case 30:
		return "Activation rejected by GGSN";
	case 31:
		return "Activation rejected, unspecified";
	case 32:
		return "Service option not supported";
	case 33:
		return "Requested service option not subscribed";
	case 34:
		return "Service option temporarily out of order";
	case 35:
		return "NSAPI already used";
	case 36:
		return "Regular PDP context deactivation";
	case 37:
		return "QoS not accepted";
	case 38:
		return "Network failure";
	case 39:
		return "Reactivation requested";
	case 40:
		return "Feature not supported";
	case 81:
		return "Invalid transaction identifier value";
	case 95:
		return "Semantically incorrect message";
	case 96:
		return "Invalid mandatory information";
	case 97:
		return "Message type non-existant or not implemented";
	case 98:
		return "Message type not comaptible with protocol state";
	case 99:
		return "Information element non-existent or not implemented";
	case 100:
		return "Conditional information element error";
	case 101:
		return "Message not compatible with protocol";
	case 111:
		return "Protocol error, unspecified";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_51_to_text(int cause)
{
	switch(cause) {
	case 3:
		return "The MS has not got any answer to the ACTIVATE PDP"
			" CONTEXT request message sent five times to the"
			" network";
	case 4:
		return "A MT PDP context which is active or in the activation"
			" process is deactivated because another MT PDP"
			" context with the same TI is requested by the network"
			" to be activated";
	case 5:
		return "A MT PDP context which is active or in the activation"
			" process is deactivated because another MT PDP"
			" context with the same TI is requested by the network"
			" to be activated. The activation request is rejected"
			" by the SM sending the cause 'insufficient resources'"
			" to the network because the SM was not able to"
			" perform the necessary comparisons for a static PDP"
			" address collision detection.";
	case 6:
		return "A MT PDP context which is active or in the activation"
			" process is deactivated because another MT PDP"
			" context with the same TI is requested by the network"
			" to be activated. As a static PDP address collision"
			" with an MO activating PDP context has been detected"
			" by the SM the SM dis- cards the activation request";
	case 7:
		return "A MT PDP context request has been indicated but could"
			" not be processed in time. The acti-vation request"
			" is rejected by the SM sending the cause"
			" 'insufficient resources' to the network.";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_127_to_text(int cause)
{
	switch(cause) {
	case 2:
		return "No detailed cause";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_241_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "Regular deactivation of the call";
	case 1:
		return "Action temporarily not allowed";
	case 2:
		return "Wrong connection type";
	case 3:
		return "Specified data service profile invalid";
	case 4:
		return "PDP type or address is unknown";
	case 5:
		return "FDN Check was not successful; GPRS Attach and PDP"
			" Context Activation blocked";
	case 255:
		return "Undefined";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_243_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "Regular call deactivation";
	case 1:
		return "LCP stopped";
	case 255:
		return "Undefined";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_248_to_text(int cause)
{
	switch(cause) {
	case 0:
		return "OK, bearer configured and started";
	case 1:
		return "Profile is locked";
	case 2:
		return "Profile ID is wrong";
	case 3:
		return "Profile is invalid";
	case 4:
		return "TCPIP error";
	case 5:
		return "Other Profile is active";
	case 6:
		return "Temporarily not allowed";
	case 7:
		return "Max number of IP-clients exceeded";
	case 8:
		return "DNS configuration error";
	case 9:
		return "Invalid client ID";
	case 10:
		return "Temporarily not allowed, IP interface is busy";
	case 11:
		return "IP interface is going down";
	case 12:
		return "Other interface is up";
	case 13:
		return "IP interface is down";
	case 20:
		return "IP connection is down";
	case 21:
		return "IP connection is coming up";
	case 22:
		return "IP connection is up";
	case 23:
		return "IP connection is going down";
	case 24:
		return "IP connection is pending";
	case 25:
		return "IP connection is not configured";
	case 30:
		return "SAIP Service already locked";
	case 31:
		return "SAIP Service locked by other instance";
	case 32:
		return "SAIP Service in wrong state";
	case 40:
		return "The specified entity ID of the controlling task"
			" is unknown to the SAIP service application.";
	case 41:
		return "The Configure Index which has been set by the"
			" controlling task with a configure request is"
			" unknown to the SAIP service application.";
	case 42:
		return "SAIP application is not writable";
	case 43:
		return "SAIP application has detected a syntax error";
	case 44:
		return "Temporarily not allowed, e.g. no Entity free or no"
			" memory available";
	case 45:
		return "Error during configuration of SAIP application";
	case 46:
		return "Internal Error of SAIP application";
	case 47:
		return "The current socket connection is not connected"
			" anymore.";
	case 255:
		return "Reason is unknown";
	default:
		return "*UNKNOWN*";
	}
}

const char *vgsm_cause_location_to_text(int location)
{
	switch(location) {
	case 0:
		return "No error";
	case 1:
		return "SIEMENS L2 cause";
	case 2:
		return "GSM cause for L3 Radio Resource Sublayer";
	case 3:
		return "SIEMENS cause for L3 Radio Resource Sublayer";
	case 4:
		return "GSM cause for L3 Mobility Management";
	case 5:
		return "SIEMENS cause for L3 Mobility Management";
	case 6:
		return "GSM cause for L3 Mobility Management via MMR-SAP";
	case 7:
		return "SIEMENS cause for L3 Mobility Management via MMR-SAP";
	case 8:
		return "GSM cause for L3 Call Control";
	case 9:
		return "SIEMENS cause for L3 Call Control";
	case 11:
		return "SIEMENS cause for L3 Advice of Charge Entity";
	case 12:
		return "GSM cause for L3 SMS CP Entity";
	case 13:
		return "SIEMENS cause for L3 SMS CP Entity";
	case 14:
		return "GSM cause for L3 SMS RL Entity";
	case 15:
		return "SIEMENS cause for L3 SMS RL Entity";
	case 16:
		return "GSM cause for L3 SMS TL Entity";
	case 17:
		return "SIEMENS cause for L3 SMS TL Entity";
	case 18:
		return "SIEMENS cause for DSM Entity";
	case 21:
		return "GSM cause for L3 Call-related Supplementary Services";
	case 22:
		return "SIEMENS cause for L3 Call-related Supplementary"
			" Services";
	case 32:
		return "SIEMENS cause for Supplementary Services Entity";
	case 33:
		return "SIEMENS cause for Supplementary Services Manager";
	case 34:
		return "Network cause for Supplementary Services";
	case 35:
		return "Supplementary Services network error";
	case 48:
		return "GSM cause for GPRS Mobility Management";
	case 49:
		return "SIEMENS cause for GPRS Mobility Management";
	case 50:
		return "GSM cause for Session Management";
	case 51:
		return "SIEMENS cause for Session Management";
	case 127:
		return "SIEMENS cause for protocol module or other local cause";
	case 128:
		return "Supplementary Services general problem";
	case 129:
		return "Supplementary Services invoke problem";
	case 130:
		return "Supplementary Services result problem";
	case 131:
		return "Supplementary Services error problem";
	case 241:
		return "SIEMENS cause for GPRS API";
	case 242:
		return "SIEMENS cause for Link Management";
	case 243:
		return "SIEMENS cause for PPP/IP-Stack";
	case 248:
		return "SIEMENS cause for IP via AT commands";
	case 1000:
		return "Asterisk";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cause_reason_1000_to_text(int cause)
{
	switch(cause) {
	case VGSM_CAUSE_REASON_NORMAL_CALL_CLEARING:
		return "Normal call clearing";
	case VGSM_CAUSE_REASON_NO_RESOURCES:
		return "No resources";
	case VGSM_CAUSE_REASON_SLCC_ERROR:
		return "Unexpected SLCC error";
	case VGSM_CAUSE_REASON_MODULE_NOT_READY:
		return "Module not ready";
	case VGSM_CAUSE_REASON_UNSUPPORTED_BEARER_TYPE:
		return "Unsupported bearer type";
	case VGSM_CAUSE_REASON_USER_BUSY:
		return "User busy";
	case VGSM_CAUSE_REASON_CONGESTION:
		return "Congestion";
	default:
		return "*UNKNOWN*";
	}
}

const char *vgsm_cause_reason_to_text(int location, int cause)
{
	switch(location) {
	case 2:
		return vgsm_cause_reason_2_to_text(cause);

	case 3:
		return vgsm_cause_reason_3_to_text(cause);

	case 4:
	case 6:
	case 50:
		return vgsm_cause_reason_4_6_50_to_text(cause);

	case 5:
	case 7:
		return vgsm_cause_reason_5_7_to_text(cause);

	case 8:
		return vgsm_cause_reason_8_to_text(cause);

	case 9:
		return vgsm_cause_reason_9_to_text(cause);

	case 11:
		return vgsm_cause_reason_11_to_text(cause);

	case 21:
		return vgsm_cause_reason_21_to_text(cause);

	case 22:
		return vgsm_cause_reason_22_to_text(cause);

	case 51:
		return vgsm_cause_reason_51_to_text(cause);

	case 127:
		return vgsm_cause_reason_127_to_text(cause);

	case 241:
		return vgsm_cause_reason_241_to_text(cause);

	case 243:
		return vgsm_cause_reason_243_to_text(cause);

	case 248:
		return vgsm_cause_reason_248_to_text(cause);

	case 1000:
		return vgsm_cause_reason_1000_to_text(cause);
	default:
		return NULL;
	}
}

int vgsm_cause_to_ast_cause(int location, int reason)
{
	switch(location) {
	case 8:
		return reason;
	/* FIXME add cause translation!!!!!!!!!!! */
	default:
		return AST_CAUSE_NETWORK_OUT_OF_ORDER;
	}
}

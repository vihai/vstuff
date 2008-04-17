/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <iconv.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/termios.h>
#include <sys/signal.h>

#include <asm/types.h>

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/causes.h>
#include <asterisk/manager.h>
#include <asterisk/version.h>

#include <linux/vgsm.h>
#include <linux/vgsm2.h>

#include "util.h"
#include "chan_vgsm.h"
#include "me.h"
#include "comm.h"
#include "causes.h"
#include "sms.h"
#include "sms_submit.h"
#include "sms_deliver.h"
#include "sms_status_report.h"
#include "cbm.h"
#include "operators.h"
#include "pin.h"
#include "sim.h"
#include "sim_file.h"
#include "timer.h"

#define FAILED_RETRY_TIME (5 * SEC)
#define READY_UPDATE_TIME (30 * SEC)
#define CLOSED_POSTPONE (3 * SEC)
#define POWERING_ON_TIMEOUT (10 * SEC)
#define RESET_TIMEOUT (15 * SEC)
#define POWERING_OFF_TIMEOUT (7 * SEC)
#define WAITING_INITIALIZATION_DELAY (2 * SEC)
#define WAITING_INITIALIZATION_SIM_INSERTED_DELAY (5 * SEC)

void vgsm_me_config_default(struct vgsm_me_config *mc)
{
	mc->flow_control = VGSM_FLOW_AUTO;

	strcpy(mc->context, "vgsm");
	strcpy(mc->pin, "");

	mc->rx_gain = 255;
	mc->tx_gain = 255;
	mc->set_clock = TRUE;
	mc->poweroff_on_exit = TRUE;
	mc->operator_selection = VGSM_OPSEL_AUTOMATIC;

	strcpy(mc->smcc_address.digits, "");

	mc->operator_mcc = -1;
	mc->operator_mnc = -1;

	strcpy(mc->sms_sender_domain, "localhost");
	strcpy(mc->sms_recipient_address, "root@localhost");

	mc->dtmf_quelch = FALSE;
	mc->dtmf_mutemax = FALSE;
	mc->dtmf_relax = FALSE;

	mc->amr_enabled = TRUE;
	mc->gsm_hr_enabled = TRUE;
	mc->gsm_preferred = VGSM_CODEC_GSM_FR;

	mc->rx_calibrate = 16384;
	mc->tx_calibrate = 21402;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#else
	static struct ast_jb_conf default_jbconf =
	{
		.flags = 0,
		.max_size = -1,
		.resync_threshold = -1,
		.impl = "",
	};

	/* Copy the default jb config over global_jbconf */
	memcpy(&mc->jbconf, &default_jbconf, sizeof(mc->jbconf));
#endif

	mc->jitbuf_average = 5;
	mc->jitbuf_maxhole = 2;
	mc->jitbuf_low = 10;
	mc->jitbuf_hardlow = 0;
	mc->jitbuf_high = 50;
	mc->jitbuf_hardhigh = 1024;

	mc->suppress_proceeding = FALSE;
}

static const char *vgsm_me_status_to_text(enum vgsm_me_status status)
{
	switch(status) {
	case VGSM_ME_STATUS_UNCONFIGURED:
		return "UNCONFIGURED";
	case VGSM_ME_STATUS_CLOSED:
		return "CLOSED";
	case VGSM_ME_STATUS_OFF:
		return "OFF";
	case VGSM_ME_STATUS_POWERING_ON:
		return "POWERING_ON";
	case VGSM_ME_STATUS_POWERING_OFF:
		return "POWERING_OFF";
	case VGSM_ME_STATUS_RESETTING:
		return "RESETTING";
	case VGSM_ME_STATUS_WAITING_INITIALIZATION:
		return "WAITING_INITIALIZATION";
	case VGSM_ME_STATUS_INITIALIZING:
		return "INITIALIZING";
	case VGSM_ME_STATUS_READY:
		return "READY";
	case VGSM_ME_STATUS_OFFLINE:
		return "OFFLINE";
	case VGSM_ME_STATUS_WAITING_SIM:
		return "WAITING_SIM";
	case VGSM_ME_STATUS_WAITING_PIN:
		return "WAITING_PIN";
	case VGSM_ME_STATUS_FAILED:
		return "FAILED";
	}

	return "*UNKNOWN*";
}

static const char *vgsm_net_status_to_text(
	enum vgsm_net_status status)
{
	switch(status) {
	case VGSM_NET_STATUS_NOT_SEARCHING:
		return "NOT_SEARCHING";
	case VGSM_NET_STATUS_NOT_REGISTERED:
		return "NOT_REGISTERED";
	case VGSM_NET_STATUS_REGISTERED_HOME:
		return "REGISTERED_HOME";
	case VGSM_NET_STATUS_UNKNOWN:
		return "UNKNOWN";
	case VGSM_NET_STATUS_REGISTRATION_DENIED:
		return "REGISTRATION_DENIED";
	case VGSM_NET_STATUS_REGISTERED_ROAMING:
		return "REGISTERED_ROAMING";
	}

	return "*UNKNOWN*";
};

static const char *vgsm_me_operator_selection_to_text(
	enum vgsm_operator_selection selection)
{
	switch(selection) {
	case VGSM_OPSEL_AUTOMATIC:
		return "Automatic";
	case VGSM_OPSEL_MANUAL:
		return "Manual";
	case VGSM_OPSEL_MANUAL_FALLBACK:
		return "Manual with fallback";
	case VGSM_OPSEL_DEREGISTERED:
		return "Deregistered";
	}

	return "*UNKNOWN*";
};

static const char *vgsm_cme_error_to_text(int code)
{
	switch(code) {
	case 0:
		return "phone failure";
	case 1:
		return "no connection to phone";
	case 2:
		return "phone-adaptor link reserved";
	case 3:
		return "operation not allowed";
	case 4:
		return "operation not supported";
	case 5:
		return "PH-SIM PIN required";
	case 6:
		return "NOT SUPPORTED";
	case 7:
		return "NOT SUPPORTED";
	case 10:
		return "SIM not inserted";
	case 11:
		return "SIM PIN required";
	case 12:
		return "SIM PUK required";
	case 13:
		return "SIM failure";
	case 14:
		return "SIM busy";
	case 15:
		return "SIM wrong";
	case 16:
		return "incorrect password";
	case 17:
		return "SIM PIN2 required";
	case 18:
		return "SIM PUK2 required";
	case 20:
		return "memory full";
	case 21:
		return "invalid index";
	case 22:
		return "not found";
	case 23:
		return "memory failure";
	case 24:
		return "text string too long";
	case 25:
		return "invalid characters in text string";
	case 26:
		return "dial string too long";
	case 27:
		return "invalid characters in dial string";
	case 30:
		return "no network service";
	case 31:
		return "network timeout";
	case 32:
		return "network not allowed - emergency calls only";
	case 40:
		return "network personalization PIN required";
	case 41:
		return "network personalization PUK required";
	case 42:
		return "network subset personalization PIN required";
	case 43:
		return "network subset personalization PUK required";
	case 44:
		return "service provider personalization PIN required";
	case 45:
		return "service provider personalization PUK required";
	case 46:
		return "corporate personalization PIN required";
	case 47:
		return "corporate personalization PUK required";
	case 48:
		return "master phone code required";
	case 100:
		return "unknown";
	case 103:
		return "illegal MS";
	case 106:
		return "illegal ME";
	case 107:
		return "GPRS service not allowed";
	case 111:
		return "PLMN not allowed";
	case 112:
		return "location area not allowed";
	case 113:
		return "roaming not allowed in this location area";
	case 132:
		return "service option not supported";
	case 133:
		return "requested service option not subscribed";
	case 134:
		return "service option temporarily out of order";
	case 148:
		return "unspecified GPRS error";
	case 149:
		return "PDP authentication failure";
	case 150:
		return "invalid mobile class";
	case 256:
		return "operation temporary not allowed";
	case 257:
		return "call barred";
	case 258:
		return "phone is busy";
	case 259:
		return "user abort";
	case 260:
		return "invalid dial string";
	case 261:
		return "ss not executed";
	case 262:
		return "SIM blocked";
	case 263:
		return "invalid block";
	case 615:
		return "network failure";
	case 616:
		return "network is down";
	case 639:
		return "service type not yet available";
	case 640:
		return "operation of service temporary not allowed";
	case 764:
		return "missing input value";
	case 765:
		return "invalid input value";
	case 767:
		return "operation failed";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_cms_error_to_text(int code)
{
	switch(code) {
	case 1:
		return "Unassigned (unallocated) number";
	case 8:
		return "Operator determined barring";
	case 10:
		return "Call barred";
	case 21:
		return "Short message transfer rejected";
	case 27:
		return "Destination out of service";
	case 28:
		return "Unidentified subscriber";
	case 29:
		return "Facility rejected";
	case 30:
		return "Unknown subscriber";
	case 38:
		return "Network out of order";
	case 41:
		return "Temporary failure";
	case 42:
		return "Congestion";
	case 47:
		return "Resources unavailable, unspecified";
	case 50:
		return "Requested facility not subscribed";
	case 69:
		return "Requested facility not implemented";
	case 81:
		return "Invalid short message transfer reference value";
	case 95:
		return "Invalid message, unspecified";
	case 96:
		return "Invalid mandatory information";
	case 97:
		return "Message type non-existent or not implemented";
	case 98:
		return "Message not compatible with short message pro";
	case 99:
		return "Information element non-existent or not impleme";
	case 111:
		return "Protocol error, unspecified";
	case 127:
		return "Interworking, unspecified";
	case 128:
		return "Telematic interworking not supported";
	case 129:
		return "Short message Type 0 not supported";
	case 130:
		return "Cannot replace short message";
	case 143:
		return "Unspecified TP-PID error";
	case 144:
		return "Data coding scheme (alphabet) not supported";
	case 145:
		return "Message class not supported";
	case 159:
		return "Unspecified TP-DCS error";
	case 160:
		return "Command cannot be actioned";
	case 161:
		return "Command unsupported";
	case 175:
		return "Unspecified TP-Command error";
	case 176:
		return "TPDU not supported";
	case 192:
		return "SC busy";
	case 193:
		return "No SC subscription";
	case 194:
		return "SC system failure";
	case 195:
		return "Invalid SME address";
	case 196:
		return "Destination SME barred";
	case 197:
		return "SM Rejected-Duplicate SM";
	case 198:
		return "TP-VPF not supported";
	case 199:
		return "TP-VP not supported";
	case 208:
		return "D0 SIM SMS storage full";
	case 209:
		return "No SMS storage capability in SIM";
	case 210:
		return "Error in MS";
	case 211:
		return "Memory Capacity Exceeded";
	case 212:
		return "SIM Application Toolkit Busy";
	case 213:
		return "SIM data download error";
	case 255:
		return "Unspecified error cause";
	case 300:
		return "ME failure";
	case 301:
		return "SMS service of ME reserved";
	case 302:
		return "Operation not allowed";
	case 303:
		return "Operation not supported";
	case 304:
		return "Invalid PDU mode parameter";
	case 305:
		return "Invalid text mode parameter";
	case 310:
		return "SIM not inserted";
	case 311:
		return "SIM PIN required";
	case 312:
		return "PH-SIM PIN required";
	case 313:
		return "SIM failure";
	case 314:
		return "SIM busy";
	case 315:
		return "SIM wrong";
	case 316:
		return "SIM PUK required";
	case 317:
		return "SIM PIN2 required";
	case 318:
		return "SIM PUK2 required";
	case 320:
		return "Memory failure";
	case 321:
		return "Invalid memory index";
	case 322:
		return "Memory full";
	case 330:
		return "SMSC address unknown";
	case 331:
		return "no network service";
	case 332:
		return "network timeout";
	case 340:
		return "no +CNMA ack expected";
	case 500:
		return "unknown error";
	case 512:
		return "User abort";
	case 513:
		return "unable to store";
	case 514:
		return "invalid status";
	case 515:
		return "invalid character in address string";
	case 516:
		return "invalid length";
	case 517:
		return "invalid character in pdu";
	case 518:
		return "invalid parameter";
	case 519:
		return "invalid length or character";
	case 520:
		return "invalid character in text";
	case 521:
		return "timer expired";
	case 522:
		return "Operation temporary not allowed";

	default:
		return "*UNKNOWN*";
	}
}

const char *vgsm_me_error_to_text(int code)
{
	if (code >= CME_ERROR_BASE &&
	    code < CME_ERROR_BASE + CME_ERROR_SIZE)
		return vgsm_cme_error_to_text(code - CME_ERROR_BASE);
	else if (code >= CMS_ERROR_BASE &&
		 code < CMS_ERROR_BASE + CMS_ERROR_SIZE)
		return vgsm_cms_error_to_text(code - CMS_ERROR_BASE);
	else if (code == VGSM_RESP_OK)
		return "OK";
	else if (code == VGSM_RESP_CONNECT)
		return "CONNECT";
	else if (code == VGSM_RESP_NO_CARRIER)
		return "NO CARRIER";
	else if (code == VGSM_RESP_ERROR)
		return "ERROR";
	else if (code == VGSM_RESP_NO_DIALTONE)
		return "NO DIALTONE";
	else if (code == VGSM_RESP_BUSY)
		return "BUSY";
	else if (code == VGSM_RESP_NO_ANSWER)
		return "NO ANSWER";
	else if (code == VGSM_RESP_UNKNOWN)
		return "UNKNOWN";
	else if (code == VGSM_RESP_TIMEOUT)
		return "Communication timeout";
	else if (code == VGSM_RESP_FAILED)
		return "Communication error";
	else
		return "*UNKNOWN*";
}

static const char *vgsm_call_state_to_text(enum vgsm_call_state state)
{
	switch(state) {
	case VGSM_CALL_STATE_UNUSED:
		return "UNUSED";
	case VGSM_CALL_STATE_ACTIVE:
		return "ACTIVE";
	case VGSM_CALL_STATE_HELD:
		return "HELD";
	case VGSM_CALL_STATE_DIALING:
		return "DIALING";
	case VGSM_CALL_STATE_ALERTING:
		return "ALERTING";
	case VGSM_CALL_STATE_INCOMING:
		return "INCOMING";
	case VGSM_CALL_STATE_WAITING:
		return "WAITING";
	case VGSM_CALL_STATE_TERMINATING:
		return "TERMINATING";
	case VGSM_CALL_STATE_DROPPED:
		return "DROPPED";
	}

	return "*INVALID*";
}

static const char *vgsm_flow_control_to_text(enum vgsm_flow_control flow)
{
	switch(flow) {
	case VGSM_FLOW_AUTO:
		return "AUTO";
	case VGSM_FLOW_NONE:
		return "NONE";
	case VGSM_FLOW_SW:
		return "SW";
	case VGSM_FLOW_HW:
		return "HW";
	}

	return "*UNKNOWN*";
}


#if 0
static const char *vgsm_call_direction_to_text(
	enum vgsm_call_direction direction)
{
	switch(direction) {
	case VGSM_CALL_DIRECTION_MOBILE_ORIGINATED:
		return "MOBILE ORIGINATED";
	case VGSM_CALL_DIRECTION_MOBILE_TERMINATED:
		return "MOBILE TERMINATED";
	}

	return "*INVALID*";
}
#endif

static const char *vgsm_call_bearer_to_text(enum vgsm_call_bearer bearer)
{
	switch(bearer) {
	case VGSM_CALL_BEARER_VOICE:
		return "VOICE";
	case VGSM_CALL_BEARER_DATA:
		return "DATA";
	case VGSM_CALL_BEARER_FAX:
		return "FAX";
	case VGSM_CALL_BEARER_VOICE_THEN_DATA_VOICE:
		return "VOICE=>DATA (VOICE)";
	case VGSM_CALL_BEARER_VOICE_ALT_DATA_VOICE:
		return "VOICE/DATA (VOICE)";
	case VGSM_CALL_BEARER_VOICE_ALT_FAX_VOICE:
		return "VOICE/FAX (FAX)";
	case VGSM_CALL_BEARER_VOICE_THEN_DATA_DATA:
		return "VOICE=>DATA (DATA)";
	case VGSM_CALL_BEARER_VOICE_ALT_DATA_DATA:
		return "VOICE/DATA (DATA)";
	case VGSM_CALL_BEARER_VOICE_ALT_FAX_FAX:
		return "VOICE/FAX (FAX)";
	case VGSM_CALL_BEARER_VOICE_UNKNOWN:
		return "UNKNOWN";
	}

	return "*INVALID*";
}

static const char *vgsm_qual_to_text(int ber)
{
	switch(ber) {
	case 0:
		return "less than 0.1%";
	case 1:
		return "0.26% => 0.30%";
	case 2:
		return "0.51% => 0.64%";
	case 3:
		return "1.0% => 1.3%";
	case 4:
		return "1.9% => 2.7%";
	case 5:
		return "3.8% => 5.4%";
	case 6:
		return "7.6% => 11%";
	case 7:
		return "greater than 15%";
	case 99:
		return "N/A";
	default:
		return "*INVALID*";
	}
}

static const char *vgsm_codec_to_text(enum vgsm_codec codec)
{
	switch(codec) {
	case VGSM_CODEC_GSM_EFR:
		return "GSM-EFR";
	case VGSM_CODEC_GSM_FR:
		return "GSM-FR";
	case VGSM_CODEC_GSM_HR:
		return "GSM-HR";
	case VGSM_CODEC_AMR_HR:
		return "AMR-HR";
	case VGSM_CODEC_AMR_FR:
		return "AMR-FR";
	}

	return "*INVALID*";
}

struct vgsm_me_config *vgsm_me_config_alloc(void)
{
	struct vgsm_me_config *me_config;

	me_config = malloc(sizeof(*me_config));
	if (!me_config)
		return NULL;

	memset(me_config, 0, sizeof(*me_config));

	me_config->refcnt = 1;

	return me_config;
}

struct vgsm_me_config *vgsm_me_config_get(
	struct vgsm_me_config *me_config)
{
	assert(me_config);
	assert(me_config->refcnt > 0);
	assert(me_config->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	me_config->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return me_config;
}

void _vgsm_me_config_put(struct vgsm_me_config *me_config)
{
	assert(me_config);
	assert(me_config->refcnt > 0);
	assert(me_config->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --me_config->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt)
		free(me_config);
}

static int vgsm_me_config_from_var(
	struct vgsm_me_config *mc,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "device")) {
		strncpy(mc->device_filename, var->value,
			sizeof(mc->device_filename));
	} else if (!strcasecmp(var->name, "mesim_device")) {
		strncpy(mc->mesim_device_filename, var->value,
			sizeof(mc->mesim_device_filename));
	} else if (!strcasecmp(var->name, "flow_control")) {
		if (!strcasecmp(var->value, "auto"))
			mc->operator_selection = VGSM_FLOW_AUTO;
		else if (!strcasecmp(var->value, "none"))
			mc->operator_selection = VGSM_FLOW_NONE;
		else if (!strcasecmp(var->value, "software"))
			mc->operator_selection = VGSM_FLOW_SW;
		else if (!strcasecmp(var->value, "hardware"))
			mc->operator_selection = VGSM_FLOW_HW;
		else {
			ast_log(LOG_ERROR,
				"Flow control selection '%s' unknown\n",
				var->value);

			return -1;
		}
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(mc->context, var->value, sizeof(mc->context));
	} else if (!strcasecmp(var->name, "pin")) {
		strncpy(mc->pin, var->value, sizeof(mc->pin));
	} else if (!strcasecmp(var->name, "rx_gain")) {
		mc->rx_gain = atoi(var->value);
	} else if (!strcasecmp(var->name, "tx_gain")) {
		mc->tx_gain = atoi(var->value);
	} else if (!strcasecmp(var->name, "set_clock")) {
		mc->set_clock = ast_true(var->value);
	} else if (!strcasecmp(var->name, "poweroff_on_exit")) {
		mc->poweroff_on_exit = ast_true(var->value);
	} else if (!strcasecmp(var->name, "sim_proto")) {
		ast_log(LOG_NOTICE,
			"sim_proto is obsolete, use sim_driver instead\n");
		goto sim_driver;
	} else if (!strcasecmp(var->name, "sim_driver")) {
sim_driver:
		if (!strcasecmp(var->value, "local"))
			mc->sim_driver_type = VGSM_MESIM_DRIVER_LOCAL;
		else if (!strcasecmp(var->value, "implementa"))
			mc->sim_driver_type = VGSM_MESIM_DRIVER_IMPLEMENTA;
		else {
			ast_log(LOG_ERROR,
				"Unknown SIM driver '%s'\n",
				var->value);

			return -1;
		}

	} else if (!strcasecmp(var->name, "sim_local_device_filename")) {
		ast_log(LOG_NOTICE,
			"sim_local_device_filename is obsolete,"
			" use sim_device_filename instead\n");
		goto sim_device_filename;
	} else if (!strcasecmp(var->name, "sim_device_filename")) {
sim_device_filename:
		strncpy(mc->sim_device_filename, var->value,
			sizeof(mc->sim_device_filename));
	} else if (!strcasecmp(var->name, "sim_client_addr")) {
		struct ast_hostent ahp;
		struct hostent *hp;

		char *addr = alloca(strlen(var->value) + 1);
		char *port = alloca(strlen(var->value) + 1);

		if (sscanf(var->value, "%[^:]:%s", addr, port) != 2) {
			ast_log(LOG_ERROR,
				"Cannot parse sim_client_addr '%s'\n",
				var->value);
			return -1;
		}

		if (!(hp = ast_gethostbyname(addr, &ahp))) {
			ast_log(LOG_ERROR, "Invalid address: %s\n",
				var->value);

			return -1;
		}

		memcpy(&mc->sim_client_addr.sin_addr,
			hp->h_addr,
			sizeof(mc->sim_client_addr.sin_addr));

		mc->sim_client_addr.sin_port = htons(atoi(port));

	} else if (!strcasecmp(var->name, "operator_selection")) {
		if (!strcasecmp(var->value, "auto"))
			mc->operator_selection = VGSM_OPSEL_AUTOMATIC;
		else if (!strcasecmp(var->value, "manual"))
			mc->operator_selection = VGSM_OPSEL_MANUAL;
		else if (!strcasecmp(var->value, "manual_fallback"))
			mc->operator_selection = VGSM_OPSEL_MANUAL_FALLBACK;
		else if (!strcasecmp(var->value, "deregistered"))
			mc->operator_selection = VGSM_OPSEL_DEREGISTERED;
		else {
			ast_log(LOG_ERROR,
				"Operator selection '%s' unknown\n",
				var->value);

			return -1;
		}

	} else if (!strcasecmp(var->name, "operator_id")) {

		mc->operator_mcc = -1;
		mc->operator_mnc = -1;

		if (strlen(var->value) &&
		    sscanf(var->value, "%03hu%hu",
				&mc->operator_mcc, &mc->operator_mnc) < 2) {

			mc->operator_mcc = -1;
			mc->operator_mnc = -1;

			ast_log(LOG_ERROR,
				"Cannot parse operator ID '%s'\n",
				var->value);

			return -1;
		}

	} else if (!strcasecmp(var->name, "sms_service_center")) {
		vgsm_number_parse(&mc->smcc_address, var->value);
	} else if (!strcasecmp(var->name, "sms_sender_domain")) {
		strncpy(mc->sms_sender_domain, var->value,
			sizeof(mc->sms_sender_domain));
	} else if (!strcasecmp(var->name, "sms_recipient_address")) {
		strncpy(mc->sms_recipient_address, var->value,
			sizeof(mc->sms_recipient_address));
	} else if (!strcasecmp(var->name, "dtmf_quelch")) {
		mc->dtmf_quelch = ast_true(var->value);
	} else if (!strcasecmp(var->name, "dtmf_mutemax")) {
		mc->dtmf_mutemax = ast_true(var->value);
	} else if (!strcasecmp(var->name, "dtmf_relax")) {
		mc->dtmf_relax = ast_true(var->value);
	} else if (!strcasecmp(var->name, "amr_enabled")) {
		mc->amr_enabled = ast_true(var->value);
	} else if (!strcasecmp(var->name, "gsm_hr_enabled")) {
		mc->gsm_hr_enabled = ast_true(var->value);
	} else if (!strcasecmp(var->name, "gsm_preferred")) {
		if (strcasecmp(var->value, "hr"))
			mc->gsm_preferred = VGSM_CODEC_GSM_HR;
		else if (strcasecmp(var->value, "fr"))
			mc->gsm_preferred = VGSM_CODEC_GSM_FR;
		else {
			ast_log(LOG_ERROR,
				"Unknown preferred coded '%s'\n",
				var->value);
			return -1;
		}
	} else if (!strcasecmp(var->name, "rx_calibrate")) {
		mc->rx_calibrate = atoi(var->value);
	} else if (!strcasecmp(var->name, "tx_calibrate")) {
		mc->tx_calibrate = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_average")) {
		mc->jitbuf_average = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_maxhole")) {
		mc->jitbuf_maxhole = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_low")) {
		mc->jitbuf_low = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_hardlow")) {
		mc->jitbuf_hardlow = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_high")) {
		mc->jitbuf_high = atoi(var->value);
	} else if (!strcasecmp(var->name, "jitbuf_hardhigh")) {
		mc->jitbuf_hardhigh = atoi(var->value);
	} else if (!strcasecmp(var->name, "suppress_proceeding")) {
		mc->suppress_proceeding = ast_true(var->value);
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#else
	} else if (!ast_jb_read_conf(&mc->jbconf, var->name, var->value)) {
#endif
	} else {
		return -1;
	}

	return 0;
}

static void vgsm_me_config_copy(
	struct vgsm_me_config *dst,
	const struct vgsm_me_config *src)
{
	strncpy(dst->device_filename, src->device_filename,
		sizeof(dst->device_filename));
	strncpy(dst->mesim_device_filename, src->mesim_device_filename,
		sizeof(dst->mesim_device_filename));
	strncpy(dst->context, src->context,
		sizeof(dst->context));
	strncpy(dst->pin, src->pin,
		sizeof(dst->pin));

	dst->rx_gain = src->rx_gain;
	dst->tx_gain = src->tx_gain;
	dst->set_clock = src->set_clock;
	dst->poweroff_on_exit = src->poweroff_on_exit;
	dst->sim_driver_type = src->sim_driver_type;
	strncpy(dst->sim_device_filename,
		src->sim_device_filename,
		sizeof(dst->sim_device_filename));
	dst->operator_selection = src->operator_selection;

	dst->operator_mcc = src->operator_mcc;
	dst->operator_mnc = src->operator_mnc;

	vgsm_number_copy(&dst->smcc_address, &src->smcc_address);

	strncpy(dst->sms_sender_domain, src->sms_sender_domain,
		sizeof(dst->sms_sender_domain));
	strncpy(dst->sms_recipient_address, src->sms_recipient_address,
		sizeof(dst->sms_recipient_address));

	dst->dtmf_quelch = src->dtmf_quelch;
	dst->dtmf_mutemax = src->dtmf_mutemax;
	dst->dtmf_relax = src->dtmf_relax;

	dst->amr_enabled = src->amr_enabled;
	dst->gsm_hr_enabled = src->gsm_hr_enabled;
	dst->gsm_preferred = src->gsm_preferred;

	dst->rx_calibrate = src->rx_calibrate;
	dst->tx_calibrate = src->tx_calibrate;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#else
	memcpy(&dst->jbconf, &src->jbconf, sizeof(dst->jbconf));
#endif

	dst->jitbuf_average = src->jitbuf_average;
	dst->jitbuf_maxhole = src->jitbuf_maxhole;
	dst->jitbuf_low = src->jitbuf_low;
	dst->jitbuf_hardlow = src->jitbuf_hardlow;
	dst->jitbuf_high = src->jitbuf_high;
	dst->jitbuf_hardhigh = src->jitbuf_hardhigh;

	dst->suppress_proceeding = src->suppress_proceeding;
}

static void *vgsm_me_monitor_thread_main(void *data);

static struct vgsm_me *vgsm_me_get_or_create(const char *name)
{
	/* If the named me does not exist, allocate and start monitor
	 * thread. Monitor thread will remain in CLOSED state until it
	 * is explicitly started.
	 */

	ast_rwlock_wrlock(&vgsm.mes_list_lock);

	struct vgsm_me *me = _vgsm_me_get_by_name(name);
	if (!me) {
		me = vgsm_me_alloc();
		if (!me) {
			ast_rwlock_unlock(&vgsm.mes_list_lock);

			ast_log(LOG_ERROR, "Cannot allocate new me %s\n",
				name);

			goto err_me_alloc;
		}

		strncpy(me->name, name, sizeof(me->name));

		int err = vgsm_comm_init(&me->comm, vgsm_me_urcs);
		if (err < 0) {
			ast_rwlock_unlock(&vgsm.mes_list_lock);
			goto err_comm_init;
		}

		list_add_tail(&vgsm_me_get(me)->node, &vgsm.mes_list);

		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		ast_pthread_create(&me->monitor_thread, &attr,
				vgsm_me_monitor_thread_main, me);
		pthread_attr_destroy(&attr);
	}

	ast_rwlock_unlock(&vgsm.mes_list_lock);

	return me;

	vgsm_comm_destroy(&me->comm);
err_comm_init:
	vgsm_me_put(me);
err_me_alloc:

	return NULL;
}

static int vgsm_me_reconfigure(
	struct vgsm_me *me,
	struct ast_config *cfg,
	const char *cat,
	const char *name)
{
	int err;

	/* Allocate a new me configuration */
	struct vgsm_me_config *mc;
	mc = vgsm_me_config_alloc();
	if (!mc) {
		ast_log(LOG_ERROR, "Cannot allocate new me config %s\n",
			name);
		err = -ENOMEM;
		goto err_me_config_alloc;
	}

	vgsm_me_config_copy(mc, vgsm.default_mc);

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)cat);
	while (var) {
		if (vgsm_me_config_from_var(mc, var) < 0) {
			ast_log(LOG_ERROR,
				"ME '%s' configuration invalid\n",
				var->name);

			err = -EINVAL;
			goto err_me_config_invalid;
		}

		var = var->next;
	}

	mc->me = vgsm_me_get(me);

	me->config_present = TRUE;
	me->new_config = vgsm_me_config_get(mc);

	vgsm_me_config_put(mc);

	return 0;

err_me_config_invalid:
	vgsm_me_config_put(mc);
err_me_config_alloc:

	return err;
}

int vgsm_me_reload(struct ast_config *cfg)
{
	/* Read default interface configuration */

	struct ast_variable *var;
	var = ast_variable_browse(cfg, VGSM_ME_GLOBAL);
	while (var) {
		if (vgsm_me_config_from_var(vgsm.default_mc, var) < 0) {
			ast_log(LOG_WARNING,
				"Global me configuration invalid\n");

			return -1;
		}

		var = var->next;
	}

	/* Mark all MEs are not present in config file */
	{
	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	struct vgsm_me *me;
	list_for_each_entry(me, &vgsm.mes_list, node)
		me->config_present = FALSE;
	ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	/* Browse config file, create missing mes and mark present mes
	 * as configured
	 */
	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (strncmp(cat, VGSM_ME_PREFIX, strlen(VGSM_ME_PREFIX)))
			continue;

		if (!strncmp(cat, VGSM_ME_GLOBAL, strlen(VGSM_ME_GLOBAL)))
			continue;

		if (strlen(cat) <= strlen(VGSM_ME_PREFIX)) {
			ast_log(LOG_WARNING,
				"Empty me name in configuration\n");

			continue;
		}

		struct vgsm_me *me = vgsm_me_get_or_create(
						cat + strlen(VGSM_ME_PREFIX));
		if (!me)
			continue;

		vgsm_me_reconfigure(me, cfg, cat,
						cat + strlen(VGSM_ME_PREFIX));
	}

	/* TODO: Removed mes not present anymore in config file */

	/* Activate new configuration */
	{
	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	struct vgsm_me *me;
	list_for_each_entry(me, &vgsm.mes_list, node) {

		ast_mutex_lock(&me->lock);
		if (me->new_config) {
			if (me->current_config) {
				vgsm_me_config_put(me->current_config);
				me->current_config = NULL;
			}

			me->current_config = me->new_config;
			me->new_config = NULL;
		}

		/* Start newly created mes */
		if (me->current_config &&
		    me->status == VGSM_ME_STATUS_UNCONFIGURED) {
			vgsm_me_set_status(me,
				VGSM_ME_STATUS_CLOSED,
				CLOSED_POSTPONE,
				" ");
		}

		ast_mutex_unlock(&me->lock);
	}
	ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return 0;
}

static void vgsm_me_timers_updated(struct vgsm_timerset *set)
{
}

static void vgsm_me_timer(void *data);

struct vgsm_me *vgsm_me_alloc(void)
{
	struct vgsm_me *me;

	me = malloc(sizeof(*me));
	if (!me)
		goto err_malloc;

	memset(me, 0, sizeof(*me));

	me->refcnt = 1;

	ast_mutex_init(&me->lock);

	INIT_LIST_HEAD(&me->stats.inbound_counters);
	INIT_LIST_HEAD(&me->stats.outbound_counters);

	me->status = VGSM_ME_STATUS_UNCONFIGURED;
	me->in_service = TRUE;

	me->monitor_thread = AST_PTHREADT_NULL;

	me->me_fd = -1;

	vgsm_timerset_init(&me->timerset, vgsm_me_timers_updated);

	vgsm_timer_init(&me->timer, &me->timerset, "me",
			vgsm_me_timer, me);

	return me;

	free(me);
err_malloc:

	return NULL;
}

struct vgsm_me *vgsm_me_get(struct vgsm_me *me)
{
	assert(me);
	assert(me->refcnt > 0);
	assert(me->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	me->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return me;
}

void _vgsm_me_put(struct vgsm_me *me)
{
	assert(me);
	assert(me->refcnt > 0);
	assert(me->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --me->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		vgsm_comm_destroy(&me->comm);

		if (me->status_reason)
			free(me->status_reason);

		struct vgsm_counter *counter, *t;
		list_for_each_entry_safe(counter, t,
				&me->stats.inbound_counters, node)
			free(counter);

		list_for_each_entry_safe(counter, t,
				&me->stats.outbound_counters, node)
			free(counter);

		free(me);
	}
}

struct vgsm_me *_vgsm_me_get_by_name(const char *name)
{
	struct vgsm_me *me;
	list_for_each_entry(me, &vgsm.mes_list, node) {

		if (!strcasecmp(me->name, name))
			return vgsm_me_get(me);
	}

	return NULL;
}

struct vgsm_me *vgsm_me_get_by_name(const char *name)
{
	struct vgsm_me *me;

	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	me = _vgsm_me_get_by_name(name);
	ast_rwlock_unlock(&vgsm.mes_list_lock);

	return me;
}

void vgsm_me_set_status(
	struct vgsm_me *me,
	enum vgsm_me_status status,
	longtime_t timeout,
	const char *fmt, ...)
{
	va_list ap;

	ast_mutex_lock(&me->lock);

	if (me->status_reason) {
		free(me->status_reason);
		me->status_reason = NULL;
	}

	if (fmt) {
		va_start(ap, fmt);
		vasprintf(&me->status_reason, fmt, ap);
		va_end(ap);
	}

	if (timeout >= 0)
		vgsm_timer_start_delta(&me->timer, timeout);
	else
		vgsm_timer_stop(&me->timer);

	if (me->status != status) {
		if (timeout >= 0) {
			vgsm_me_debug_state(me,
				"changed state from %s to %s"
				" (timeout %.2fs)\n",
				vgsm_me_status_to_text(me->status),
				vgsm_me_status_to_text(status),
				timeout / 1000000.0);
		} else {
			vgsm_me_debug_state(me,
				"changed state from %s to %s\n",
				vgsm_me_status_to_text(me->status),
				vgsm_me_status_to_text(status));
		}

		manager_event(EVENT_FLAG_CALL, "vgsm_me_state",
			"X-vGSM-ME-Old-State: %s\r\n"
			"X-vGSM-ME-State: %s\r\n"
			"X-vGSM-ME-State-Change-Reason: %s\r\n",
			vgsm_me_status_to_text(me->status),
			vgsm_me_status_to_text(status),
			me->status_reason);
	}

	me->status = status;

	if (status == VGSM_ME_STATUS_FAILED) {
		me->failure_count++;
		me->failure_attempts++;
	}

	if (me->monitor_thread != AST_PTHREADT_NULL)
		pthread_kill(me->monitor_thread, SIGURG);

	ast_mutex_unlock(&me->lock);
}

void vgsm_me_failed_text(struct vgsm_me *me,
	const char *fmt,
	...)
{
	va_list ap;
	char tmpstr[512];

	va_start(ap, fmt);
	vsnprintf(tmpstr, sizeof(tmpstr), fmt, ap);
	va_end(ap);

	vgsm_me_set_status(me,
		VGSM_ME_STATUS_FAILED, FAILED_RETRY_TIME,
		tmpstr);
}

void vgsm_me_failed(struct vgsm_me *me, int err)
{
	ast_mutex_lock(&me->lock);

	if (err == VGSM_RESP_FAILED)
		vgsm_me_set_status(me,
			VGSM_ME_STATUS_FAILED, FAILED_RETRY_TIME,
			"Communication error");
	else
		vgsm_me_set_status(me,
			VGSM_ME_STATUS_FAILED, FAILED_RETRY_TIME,
			"Unexpected error: '%s'",
			vgsm_me_error_to_text(err));

	ast_mutex_unlock(&me->lock);
}

static char *vgsm_me_completion(const char *line, const char *word, int state)
{
	int which = 0;

	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	struct vgsm_me *me;
	list_for_each_entry(me, &vgsm.mes_list, node) {
		if (!strncasecmp(word, me->name, strlen(word)) &&
		    ++which > state) {
			ast_rwlock_unlock(&vgsm.mes_list_lock);
			return strdup(me->name);
		}
	}
	ast_rwlock_unlock(&vgsm.mes_list_lock);

	return NULL;
}

static void vgsm_me_ignite(
	struct vgsm_me *me)
{
	if (me->interface_version == 2)
		vgsm_mesim_get_ready_for_poweron(&me->mesim);

	if (ioctl(me->me_fd, VGSM_IOC_POWER_IGN, 0) < 0)
		vgsm_me_failed_text(me,
			"Error turning on me: ioctl(POWER_IGN): %s",
			strerror(errno));
}

static void vgsm_me_emerg_off(
	struct vgsm_me *me)
{
	if (ioctl(me->me_fd, VGSM_IOC_POWER_EMERG_OFF, 0) < 0)
		vgsm_me_failed_text(me,
			"Error turning off me: ioctl(POWER_EMERG_OFF): %s",
			strerror(errno));
}

#define to_me(cm) container_of((cm), struct vgsm_me, comm)

void vgsm_me_counter_inc(
	struct vgsm_me *me,
	BOOL outbound,
	int location,
	int reason)
{
	struct list_head *list;

	ast_mutex_lock(&me->lock);

	if (outbound)
		list = &me->stats.outbound_counters;
	else
		list = &me->stats.inbound_counters;

	struct vgsm_counter *counter;
	list_for_each_entry(counter, list, node) {
		if (counter->location == location &&
		    counter->reason == reason)
			goto found;
	}

	counter = malloc(sizeof(*counter));
	if (!counter) {
		ast_mutex_unlock(&me->lock);
		goto err_malloc;
	}

	memset(counter, 0, sizeof(*counter));
	counter->location = location;
	counter->reason = reason;
	counter->count = 0;

	list_add_tail(&counter->node, list);
	
found:
	counter->count++;

	ast_mutex_unlock(&me->lock);

	return;

err_malloc:

	return;
}

static int vgsm_retrieve_ceer(
	struct vgsm_me *me,
	int *location,
	int *reason)
{
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, "AT+CEER");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_cerr;
	}

	const char *pars_ptr =
		vgsm_req_first_line(req)->text + strlen("+CEER: ");
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_cerr;

	*location = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_cerr;

	*reason = atoi(field);

	vgsm_req_put(req);

//	if (!get_token(&pars_ptr, field, sizeof(field)))
//		goto done;
//
//	int ssreason = atoi(field);

	return 0;

err_cerr:
	vgsm_req_put(req);

	return -1;
}

static void vgsm_me_received_hangup(struct vgsm_me *me)
{
	vgsm_me_debug_call(me, "received hangup\n");

	/* First of all, retrieve the cause code. No-one will place
	 * calls in between, because me->vgsm_chan is still set
	 */

	int location = 8;
	int reason = 16;
	int err;

	err = vgsm_retrieve_ceer(me, &location, &reason);

	ast_mutex_lock(&me->lock);

	/* Next step, we take a reference to vgsm_chan and detach it from
	 * the me, after having updated the me's counters.
	 */

	struct vgsm_chan *vgsm_chan = vgsm_chan_get(me->vgsm_chan);

	if (err >= 0) {
		vgsm_me_counter_inc(me,
			vgsm_chan ? vgsm_chan->outbound : TRUE,
			location, reason);

		vgsm_me_debug_call(me,
			"Call released, location '%s', cause '%s'\n",
			vgsm_cause_location_to_text(location),
			vgsm_cause_reason_to_text(location, reason));
	}

	me->call_present = FALSE;

	if (me->vgsm_chan) {
		/* Detach channel from me */
		vgsm_chan_put(me->vgsm_chan);
		me->vgsm_chan = NULL;
	}
	ast_mutex_unlock(&me->lock);

	/* From now on, the me is officially free, other PBX threads
	 * may place calls on the me.
	 */

	if (vgsm_chan) {

		/* Okay, there was a vgsm_chan active on that me, let's
		 * hang it up.
		 */

		struct ast_channel *ast_chan = vgsm_chan->ast_chan;

		ast_mutex_lock(&ast_chan->lock);
		if (ast_chan->_state == AST_STATE_RESERVED) {
			ast_mutex_unlock(&ast_chan->lock);
			/* If only a +CRING has been received, no pbx has yet
			 * been started on the ast_chan, so, do a manual
			 * cleanup
			 */

			vgsm_me_put(vgsm_chan->me);
			vgsm_chan->me = NULL;

			vgsm_chan_put(ast_chan->tech_pvt);
			ast_chan->tech_pvt = NULL;
			ast_channel_free(ast_chan);
			ast_chan = NULL;
		} else {
			ast_chan->hangupcause =
				vgsm_cause_to_ast_cause(location, reason);

			ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
			ast_mutex_unlock(&ast_chan->lock);
		}

		vgsm_chan_put(vgsm_chan);
	}

	vgsm_me_debug_call(me, "received hangup done\n");
}

static void handle_unsolicited_no_carrier(
	const struct vgsm_req *urc)
{
/*	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	vgsm_me_received_hangup(me);*/
}

static void handle_unsolicited_no_dialtone(
	const struct vgsm_req *urc)
{
/*	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	vgsm_me_received_hangup(me);*/
}

static void handle_unsolicited_busy(
	const struct vgsm_req *urc)
{
/*	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	vgsm_me_received_hangup(me);*/
}

static void handle_unsolicited_ring(
	const struct vgsm_req *urc)
{
	ast_log(LOG_ERROR, "Unexpected RING\n");
}

void vgsm_me_chup_complete(struct vgsm_req *req, void *data)
{
	struct vgsm_me *me = data;

	/* Detach the vgsm_chan from me */

	ast_mutex_lock(&me->lock);
	me->call_present = FALSE;
	ast_mutex_unlock(&me->lock);

	if (req->err != VGSM_RESP_OK &&
	    req->err != VGSM_RESP_NO_CARRIER) {

		// NO CARRIER is not documented!

		ast_log(LOG_ERROR,
			"vgsm: %s: Error hanging up: %s (%d)\n",
			me->name,
			vgsm_me_error_to_text(req->err), req->err);
	}

	vgsm_me_put(me);
}

static void handle_unsolicited_cring(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&me->lock);

	me->call_present = TRUE;

	if (me->status != VGSM_ME_STATUS_READY) {
		ast_log(LOG_NOTICE,
			"Rejecting RING on not ready me\n");

		_vgsm_req_put(vgsm_req_make_callback(&me->comm,
				vgsm_me_chup_complete,
				vgsm_me_get(me),
				5 * SEC, "AT+CHUP"));

		vgsm_me_counter_inc(me,
			TRUE,
			VGSM_CAUSE_LOCATION_LOCAL,
			38);

		goto err_me_not_ready;
	}

	if (strcmp(pars, "VOICE")) {
		ast_log(LOG_NOTICE, "Not a voice call, rejecting\n");

		_vgsm_req_put(vgsm_req_make_callback(&me->comm,
				vgsm_me_chup_complete,
				vgsm_me_get(me),
				5 * SEC, "AT+CHUP"));

		vgsm_me_counter_inc(me,
			TRUE,
			VGSM_CAUSE_LOCATION_LOCAL,
			65);

		goto err_not_voice;
	}

	if (me->vgsm_chan) {
		vgsm_me_debug_call(me,
			"Received +CRING with an already active call\n");
		goto err_call_already_present;
	}

	me->stats.inbound++;

	me->vgsm_chan = vgsm_alloc_inbound_call(me);
	if (!me->vgsm_chan) {
		_vgsm_req_put(vgsm_req_make_callback(&me->comm,
				vgsm_me_chup_complete,
				vgsm_me_get(me),
				5 * SEC, "AT+CHUP"));

		vgsm_me_counter_inc(me,
			TRUE,
			VGSM_CAUSE_LOCATION_LOCAL,
			38);

		goto err_alloc_call;
	}

	ast_mutex_unlock(&me->lock);

	return;

err_alloc_call:
err_call_already_present:
err_not_voice:
err_me_not_ready:

	ast_mutex_unlock(&me->lock);

	return;
}

static void vgsm_update_me_by_creg(
	struct vgsm_me *me,
	const char *pars,
	int with_mode)
{
	const char *pars_ptr = pars;
	char field[32];

	if (with_mode) {
		if (!get_token(&pars_ptr, field, sizeof(field)))
			goto err_no_mode;
	}

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_no_status;

	int old_status = me->net.status;

	switch(atoi(field)) {
	case 0: me->net.status = VGSM_NET_STATUS_NOT_SEARCHING; break;
	case 1: me->net.status = VGSM_NET_STATUS_REGISTERED_HOME; break;
	case 2: me->net.status = VGSM_NET_STATUS_NOT_REGISTERED; break;
	case 3: me->net.status = VGSM_NET_STATUS_REGISTRATION_DENIED; break;
	case 4: me->net.status = VGSM_NET_STATUS_UNKNOWN; break;
	case 5: me->net.status = VGSM_NET_STATUS_REGISTERED_ROAMING; break;
	}

	if (me->net.status != old_status) {
		manager_event(EVENT_FLAG_CALL, "vgsm_net_state",
			"X-vGSM-GSM-Registration: %s\r\n",
			vgsm_net_status_to_text(me->net.status));

		vgsm_me_debug_state(me,
			"registration %s\n",
			vgsm_net_status_to_text(me->net.status));
	}

	if (me->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    me->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		// Update Net Info FIXME TODO
	}

	return;

err_no_status:
err_no_mode:

	return;
}

static int vgsm_update_cops(
	struct vgsm_me *me)
{
	int err;

	struct vgsm_req *req;

	req = vgsm_req_make_wait(&me->comm, 180 * SEC, "AT+COPS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_cops;
	}

	char field[10];
	const char *line = vgsm_req_first_line(req)->text;
	if (strlen(line) <= 7)
		goto parsing_complete;

	line += 7;

	/* Mode */
	if (!get_token(&line, field, sizeof(field)))
		goto parsing_complete;

	/* Format */
	if (!get_token(&line, field, sizeof(field)))
		goto parsing_complete;

	/* Current operator */
	if (!get_token(&line, field, sizeof(field)))
		goto parsing_complete;

	if (sscanf(field, "%03hu%hu", &me->net.mcc, &me->net.mnc) < 2)
		goto err_cops;

parsing_complete:
	vgsm_req_put(req);

	return 0;

err_cops:

	return -1;
}

static void handle_unsolicited_creg(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&me->lock);

	vgsm_update_me_by_creg(me, pars, FALSE);

	if (me->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    me->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING)
		vgsm_update_cops(me);

	ast_mutex_unlock(&me->lock);
}

static void handle_unsolicited_cusd(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_ccwa(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_clip(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	ast_mutex_lock(&me->lock);

	if (!me->vgsm_chan) {
		ast_log(LOG_WARNING, "Received +CLIP without an active call\n");

		ast_mutex_unlock(&me->lock);
		goto err_call_not_present;
	}

	struct vgsm_chan *vgsm_chan = vgsm_chan_get(me->vgsm_chan);
	struct ast_channel *ast_chan = vgsm_chan->ast_chan;

	ast_mutex_unlock(&me->lock);

	ast_mutex_lock(&ast_chan->lock);
	if (ast_chan->_state == AST_STATE_RING) {

		ast_mutex_unlock(&ast_chan->lock);

		vgsm_me_debug_call(me,
			"Call is already ringing, ignoring further +CRINGs\n");

		goto already_ringing;
	}

	if (ast_chan->_state != AST_STATE_RESERVED) {

		ast_mutex_unlock(&ast_chan->lock);

		vgsm_me_debug_call(me,
			"Received +CLIP but active call"
			" is not in RESERVED state (state=%d)\n",
			ast_chan->_state);

		goto err_call_not_ring;
	}


	ast_setstate(vgsm_chan->ast_chan, AST_STATE_RING);
	vgsm_chan->ast_chan->rings = 1;

	ast_chan->cid.cid_pres =
		AST_PRES_USER_NUMBER_UNSCREENED |
		AST_PRES_UNAVAILABLE;

	if (!get_token(&pars_ptr, field, sizeof(field))) {

		ast_mutex_unlock(&ast_chan->lock);

		ast_log(LOG_WARNING, "Cannot parse CID '%s'\n", pars);
		goto err_parse_cid;
	}

	ast_chan->cid.cid_num = strdup(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	ast_chan->cid.cid_ton = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	ast_chan->cid.cid_name = strdup(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	switch(atoi(field)) {
	case 0:
		ast_chan->cid.cid_pres =
			AST_PRES_USER_NUMBER_PASSED_SCREEN |
			AST_PRES_ALLOWED;
	break;

	case 1:
		ast_chan->cid.cid_pres =
			AST_PRES_USER_NUMBER_PASSED_SCREEN |
			AST_PRES_RESTRICTED;
	break;

	case 2:
		ast_chan->cid.cid_pres =
			AST_PRES_USER_NUMBER_UNSCREENED |
			AST_PRES_UNAVAILABLE;
	break;
	}

parsing_done:

	ast_mutex_unlock(&ast_chan->lock);

	if (ast_pbx_start(ast_chan)) {
		ast_log(LOG_ERROR, "Unable to start PBX on %s\n",
				ast_chan->name);
		goto err_start_pbx;
	}

	vgsm_chan_put(vgsm_chan);

	return;

err_start_pbx:
err_parse_cid:
err_call_not_ring:
already_ringing:
	vgsm_chan_put(vgsm_chan);
err_call_not_present:

	return;
}

static void handle_unsolicited_colp(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cccm(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cssi(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cssu(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_alarm(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cgreg(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cmti(
	const struct vgsm_req *urc)
{
	ast_log(LOG_ERROR, "Unexptected CMTI!\n");
}

static void vgsm_me_handle_sms_deliver(struct vgsm_sms_deliver *sms)
{
	if (sms->me->debug_sms)
		vgsm_sms_deliver_dump(sms);

	vgsm_sms_deliver_manager(sms);

	if (vgsm_sms_deliver_spool(sms) >= 0) {

		/* Send Acknowledgment */

		int err = vgsm_req_make_wait_result(&sms->me->comm,
				20 * SEC, "AT+CNMA=0");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error acknowledging SMS: %s (%d)\n",
				vgsm_me_error_to_text(err), err);
		}
	}

}

static void handle_unsolicited_cmt(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const struct vgsm_req_line *line = vgsm_req_first_line(urc);
	const char *pars = line->text + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_WARNING, " '%s'\n", pars);
		goto err_parse_length;
	}

//	int length = atoi(field);

	if (line->node.next == &urc->lines) {
		ast_log(LOG_ERROR, "Missing CMT second line\n");
		goto err_missing_line2;
	}

	struct vgsm_req_line *line2 =
		list_entry(line->node.next, struct vgsm_req_line, node);

	struct vgsm_sms_deliver *sms;
	sms = vgsm_sms_deliver_init_from_pdu(line2->text);
	if (!sms)
		goto err_decode_failed;

	sms->me = vgsm_me_get(me);

	vgsm_me_handle_sms_deliver(sms);

	vgsm_sms_deliver_put(sms);

	return;

err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static int handle_cmt_end(
	const struct vgsm_req *urc)
{
	return TRUE;
}

static void vgsm_me_handle_cbm(struct vgsm_cbm *cbm)
{
	assert(cbm->me);

	if (cbm->me->debug_cbm)
		vgsm_cbm_dump(cbm);
}

static void handle_unsolicited_cbm(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const struct vgsm_req_line *line = vgsm_req_first_line(urc);
	const char *pars = line->text + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_WARNING, "Cannot parse CBM length: '%s'\n", pars);
		goto err_parse_length;
	}

//	int length = atoi(field);

	if (line->node.next == &urc->lines) {
		ast_log(LOG_ERROR, "Missing CMT second line\n");
		goto err_missing_line2;
	}

	struct vgsm_req_line *line2 =
		list_entry(line->node.next, struct vgsm_req_line, node);

	struct vgsm_cbm *cbm = vgsm_decode_cbm_pdu(line2->text);
	if (!cbm)
		goto err_decode_failed;

	cbm->me = me;

	vgsm_me_handle_cbm(cbm);

	vgsm_cbm_put(cbm);

	return;

err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static int handle_cbm_end(
	const struct vgsm_req *urc)
{
	return TRUE;
}

static void vgsm_me_handle_sms_status_report(
	struct vgsm_sms_status_report *sms)
{
	if (sms->me->debug_sms)
		vgsm_sms_status_report_dump(sms);

	if (vgsm_sms_status_report_spool(sms) >= 0) {

		/* Send Acknowledgment */

		int err = vgsm_req_make_wait_result(&sms->me->comm,
				20 * SEC, "AT+CNMA=0");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error acknowledging SMS-STATUS-REPORT:"
				" %s (%d)\n",
				vgsm_me_error_to_text(err), err);
		}
	}

}

static void handle_unsolicited_cds(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const struct vgsm_req_line *line = vgsm_req_first_line(urc);
	const char *pars = line->text + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_WARNING, " '%s'\n", pars);
		goto err_parse_length;
	}

//	int length = atoi(field);

	if (line->node.next == &urc->lines) {
		ast_log(LOG_ERROR, "Missing CDS second line\n");
		goto err_missing_line2;
	}

	struct vgsm_req_line *line2 =
		list_entry(line->node.next, struct vgsm_req_line, node);

	struct vgsm_sms_status_report *sms;
	sms = vgsm_sms_status_report_init_from_pdu(line2->text);
	if (!sms)
		goto err_decode_failed;

	sms->me = vgsm_me_get(me);

	vgsm_me_handle_sms_status_report(sms);

	vgsm_sms_status_report_put(sms);

	return;

err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static int handle_cds_end(
	const struct vgsm_req *urc)
{
	return TRUE;
}

static void handle_unsolicited_cdsi(
	const struct vgsm_req *urc)
{
	ast_log(LOG_ERROR, "Unexpected CMTI!\n");
}

static void vgsm_handle_slcc_update(
	struct vgsm_me *me,
	struct vgsm_call *call)
{
	ast_mutex_lock(&me->lock);
	struct vgsm_chan *vgsm_chan = vgsm_chan_get(me->vgsm_chan);
	ast_mutex_unlock(&me->lock);

	vgsm_me_debug_call(me,
		"Call changed state from %s to %s\n",
		vgsm_call_state_to_text(call->prev_state),
		vgsm_call_state_to_text(call->state));

	switch(call->state) {
	case VGSM_CALL_STATE_UNUSED:
		if (vgsm_chan) {
			vgsm_me_debug_call(me,
				"Call disappeared from SLCC,"
				" requesting HANGUP\n");

			vgsm_me_received_hangup(me);
		}
	break;

	case VGSM_CALL_STATE_ACTIVE: {
		if (!vgsm_chan) {
			ast_log(LOG_ERROR, "Call is active but there is no"
				" current call\n");
			_vgsm_req_put(vgsm_req_make_callback(&me->comm,
					vgsm_me_chup_complete,
					vgsm_me_get(me),
					5 * SEC, "AT+CHUP"));

			vgsm_me_counter_inc(me,
				FALSE,
				VGSM_CAUSE_LOCATION_LOCAL,
				41);

			break;
		}

		struct ast_channel *ast_chan = vgsm_chan->ast_chan;
		ast_mutex_lock(&ast_chan->lock);

		if (ast_chan->_state != AST_STATE_UP)
			ast_setstate(ast_chan, AST_STATE_UP);

		ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
		ast_mutex_unlock(&ast_chan->lock);
	}
	break;

	case VGSM_CALL_STATE_HELD:
		ast_log(LOG_WARNING, "Unsupported state 1-held\n");
	break;

	case VGSM_CALL_STATE_DIALING:
		if (!vgsm_chan) {
			ast_log(LOG_ERROR, "Call is dialing but there is no"
				" current call\n");
			_vgsm_req_put(vgsm_req_make_callback(&me->comm,
					vgsm_me_chup_complete,
					vgsm_me_get(me),
					5 * SEC, "AT+CHUP"));

			vgsm_me_counter_inc(me,
				FALSE,
				VGSM_CAUSE_LOCATION_LOCAL,
				41);

			break;
		}

		ast_mutex_lock(&vgsm_chan->ast_chan->lock);
		if (vgsm_connect_channel(vgsm_chan) < 0) {
			ast_softhangup(vgsm_chan->ast_chan,
					AST_SOFTHANGUP_DEV);
		} else if (!vgsm_chan->mc->suppress_proceeding) {
			ast_queue_control(vgsm_chan->ast_chan,
				AST_CONTROL_PROCEEDING);
		}
		ast_mutex_unlock(&vgsm_chan->ast_chan->lock);
	break;

	case VGSM_CALL_STATE_ALERTING: {
		if (!vgsm_chan) {
			ast_log(LOG_ERROR,
				"Call is alerting but there is no"
				" current call\n");

			_vgsm_req_put(vgsm_req_make_callback(&me->comm,
					vgsm_me_chup_complete,
					vgsm_me_get(me),
					5 * SEC, "AT+CHUP"));

			vgsm_me_counter_inc(me,
				FALSE,
				VGSM_CAUSE_LOCATION_LOCAL,
				41);

			break;
		}

		struct ast_channel *ast_chan = vgsm_chan->ast_chan;
		ast_mutex_lock(&ast_chan->lock);

		if (ast_chan->_state != AST_STATE_RINGING)
			ast_setstate(ast_chan, AST_STATE_RINGING);

		ast_queue_control(ast_chan, AST_CONTROL_RINGING);
		ast_mutex_unlock(&ast_chan->lock);
	}
	break;

	case VGSM_CALL_STATE_INCOMING:
	break;

	case VGSM_CALL_STATE_WAITING:
		ast_log(LOG_ERROR, "Unsupported state 5-waiting\n");
	break;

	case VGSM_CALL_STATE_TERMINATING:
		/* Send DISCONNECT indication? */
	break;

	case VGSM_CALL_STATE_DROPPED:
		if (vgsm_chan) {
			struct ast_channel *ast_chan = vgsm_chan->ast_chan;
			ast_queue_control(ast_chan, AST_CONTROL_DISCONNECT);
		}
	break;
	}

	vgsm_chan_put(vgsm_chan);
}

static void handle_unsolicited_ciev_battchg(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Battery level: %s\n", pars);
}

static void handle_unsolicited_ciev_signal(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Signal: %s\n", pars);
}

static void handle_unsolicited_ciev_service(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Service: %s\n", pars);
}

static void handle_unsolicited_ciev_sounder(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Sounder: %s\n", pars);
}

static void handle_unsolicited_ciev_message(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Message: %s\n", pars);
}

static void handle_unsolicited_ciev_call(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	int err;

retry_workaround:;
	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SLCC");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		return;
	}

	ast_mutex_lock(&me->lock);

	int i;
	for (i=0; i<ARRAY_SIZE(me->calls); i++)
		me->calls[i].updated = FALSE;

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node) {
		const char *pars = line->text + strlen("^SLCC: ");
		struct vgsm_call call;

		if (!strcmp(line->text, "OK"))
			break;

		if (strncmp(line->text, "^SLCC: ", 7)) {
			ast_mutex_unlock(&me->lock);
			ast_log(LOG_ERROR,
				"Unexpected response %s to AT^SLCC\n",
				line->text);
			goto retry_workaround;
		}

		const char *pars_ptr = pars;
		char field[32];

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' id\n",
				line->text);
			continue;
		}

		int idx = atoi(field);

		if (idx == 0) {
			ast_log(LOG_ERROR, "SLCC index 0 is not supported\n");
			continue;
		} else if (idx >= ARRAY_SIZE(me->calls)) {
			ast_log(LOG_ERROR, "SLCC describes call index %d but"
				" a maximum of %u calls is handled\n",
				idx, (unsigned int)ARRAY_SIZE(me->calls));
			continue;
		}

		idx--;

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' direction\n",
				line->text);
			continue;
		}

		switch(atoi(field)) {
		case 0:
			call.direction = VGSM_CALL_DIRECTION_MOBILE_ORIGINATED;
		break;
		case 1:
			call.direction = VGSM_CALL_DIRECTION_MOBILE_TERMINATED;
		break;
		default:
			ast_log(LOG_ERROR, "Unhandled SLCC direction %d\n",
				atoi(field));
			continue;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse RLCC '%s' state\n",
				line->text);
			continue;
		}

		switch(atoi(field)) {
		case 0: call.state = VGSM_CALL_STATE_ACTIVE; break;
		case 1: call.state = VGSM_CALL_STATE_HELD; break;
		case 2: call.state = VGSM_CALL_STATE_DIALING; break;
		case 3: call.state = VGSM_CALL_STATE_ALERTING; break;
		case 4: call.state = VGSM_CALL_STATE_INCOMING; break;
		case 5: call.state = VGSM_CALL_STATE_WAITING; break;
		case 6: call.state = VGSM_CALL_STATE_TERMINATING; break;
		case 7: call.state = VGSM_CALL_STATE_DROPPED; break;
		default:
			ast_log(LOG_ERROR, "Unhandled SLCC state %d\n",
				atoi(field));
			continue;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' mode\n",
				line->text);
			continue;
		}

		switch(atoi(field)) {
		case 0: call.bearer = VGSM_CALL_BEARER_VOICE; break;
		case 1: call.bearer = VGSM_CALL_BEARER_DATA; break;
		case 2: call.bearer = VGSM_CALL_BEARER_FAX; break;
		case 3: call.bearer =
				VGSM_CALL_BEARER_VOICE_THEN_DATA_VOICE;
		break;
		case 4: call.bearer =
				VGSM_CALL_BEARER_VOICE_ALT_DATA_VOICE;
		break;
		case 5: call.bearer =
				VGSM_CALL_BEARER_VOICE_ALT_FAX_VOICE;
		break;
		case 6: call.bearer =
				VGSM_CALL_BEARER_VOICE_THEN_DATA_DATA;
		break;
		case 7: call.bearer =
				VGSM_CALL_BEARER_VOICE_ALT_DATA_DATA;
		break;
		case 8: call.bearer =
				VGSM_CALL_BEARER_VOICE_ALT_FAX_FAX;
		break;
		case 9: call.bearer =
				VGSM_CALL_BEARER_VOICE_UNKNOWN;
		break;
		default:
			ast_log(LOG_ERROR, "Unhandled SLCC bearer mode %d\n",
				atoi(field));
			continue;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' <mpty>\n",
				line->text);
			continue;
		}

		switch(atoi(field)) {
		case 0: call.multiparty = FALSE; break;
		case 1: call.multiparty = TRUE; break;
		default:
			ast_log(LOG_ERROR,
				"Unhandled SLCC multiparty value %d\n",
				atoi(field));
			continue;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s'"
				" <traffic channel assigned>\n",
				line->text);
			continue;
		}

		switch(atoi(field)) {
		case 0: call.channel_assigned = FALSE; break;
		case 1: call.channel_assigned = TRUE; break;
		default:
			ast_log(LOG_ERROR,
				"Unhandled SLCC <traffic channel assigned>"
				" value %d\n",
				atoi(field));
			continue;
		}

		me->calls[idx].updated = TRUE;
		me->calls[idx].direction = call.direction;
		me->calls[idx].prev_state = me->calls[idx].state;
		me->calls[idx].state = call.state;
		me->calls[idx].bearer = call.bearer;
		me->calls[idx].multiparty = call.multiparty;
		me->calls[idx].channel_assigned = call.channel_assigned;
	}

	vgsm_req_put(req);

	for (i=0; i<ARRAY_SIZE(me->calls); i++) {
		if (!me->calls[i].updated) {
			me->calls[i].prev_state = me->calls[i].state;
			me->calls[i].state = VGSM_CALL_STATE_UNUSED;
			/* Call removed */
		}
	}

	ast_mutex_unlock(&me->lock);

	/* There is a race condition here! me->calls[0] may change! FIXME */

	if (me->calls[0].prev_state != me->calls[0].state)
		vgsm_handle_slcc_update(me, &me->calls[0]);
}

static void handle_unsolicited_ciev_roam(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Roaming: %s\n", pars);
}

static void handle_unsolicited_ciev_smsfull(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "SMS memory full: %s\n", pars);
}

static int vgsm_me_update_common_cell_info(
	struct vgsm_me *me,
	struct vgsm_net_cell *cell,
	const char **pars_ptr,
	const char *line)
{
	char field[32];

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse MCC '%s'\n", line);
		goto err_moni;
	}
	
	if (sscanf(field, "%hu", &cell->mcc) != 1)
		cell->mcc = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse MNC '%s'\n", line);
		goto err_moni;
	}
	
	if (sscanf(field, "%hu", &cell->mnc) != 1)
		cell->mnc = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse LAC '%s'\n", line);
		goto err_moni;
	}
	
	if (sscanf(field, "%hx", &cell->lac) != 1)
		cell->lac = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CID '%s'\n", line);
		goto err_moni;
	}
	
	if (sscanf(field, "%hx", &cell->id) != 1)
		cell->id = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse BSIC '%s'\n", line);
		goto err_moni;
	}

	if (sscanf(field, "%hu", &cell->bsic) != 1)
		cell->bsic = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse ARFCN '%s'\n", line);
		goto err_moni;
	}

	if (sscanf(field, "%hu", &cell->arfcn) != 1)
		cell->arfcn = 0;

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLev '%s'\n", line);
		goto err_moni;
	}

	if (sscanf(field, "%hu", &cell->rx_lev) != 1)
		cell->rx_lev = 0;

	return 0;

err_moni:

	return -1;
}

static int vgsm_update_smond(struct vgsm_me *me)
{
	struct vgsm_comm *comm = &me->comm;
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SMOND");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK)
		goto err_moni;

	const char *line;
	line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("^SMOND:");
	char field[32];

	if (!strlen(pars_ptr))
		goto err_moni;

	ast_mutex_lock(&me->lock);

	if (vgsm_me_update_common_cell_info(me, &me->net.sci,
							&pars_ptr, line) < 0)
		goto err_moni2;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevFull '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.rx_lev_full) != 1)
		me->net.sci2.rx_lev_full = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevSub '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.rx_lev_sub) != 1)
		me->net.sci2.rx_lev_sub = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQual '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.rx_qual_full) != 1)
		me->net.sci2.rx_qual_full = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualFull '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.rx_qual_sub) != 1)
		me->net.sci2.rx_qual_sub = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualSub '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.timeslot) != 1)
		me->net.sci2.timeslot = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse Timeslot '%s'\n", line);
		goto err_moni2;
	}

	me->net.ncells = 0;

	int i;
	for (i=0; i<6; i++) {
		if (vgsm_me_update_common_cell_info(me,
				&me->net.nci[me->net.ncells],
				&pars_ptr, line) < 0)
			goto err_moni2;

		if (me->net.nci[me->net.ncells].mnc != 0)
			me->net.ncells++;
	}

	assert(me->net.ncells <= 6);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse TA '%s'\n", line);
		goto err_moni2;
	}

	if (sscanf(field, "%d", &me->net.sci2.ta) != 1)
		me->net.sci2.ta = 0;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RSSI '%s'\n", line);
		goto err_moni2;
	}

	if (strlen(field)) {
		if (sscanf(field, "%d", &me->net.sci2.rssi) != 1)
			me->net.sci2.rssi = 0;

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse BER '%s'\n", line);
			goto err_moni2;
		}

		if (sscanf(field, "%d", &me->net.sci2.ber) != 1)
			me->net.sci2.ber = 0;
	}

	vgsm_req_put(req);

	ast_mutex_unlock(&me->lock);

	return 0;
	
err_moni2:
	ast_mutex_unlock(&me->lock);
err_moni:
	vgsm_req_put(req);

	return -1;
}

static void handle_unsolicited_ciev_rssi(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	ast_mutex_lock(&me->lock);
	if (me->status == VGSM_ME_STATUS_READY)
		vgsm_update_smond(me);
	ast_mutex_unlock(&me->lock);
}

static void handle_unsolicited_ciev_audio(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Audio: %s\n", pars);
}

static void handle_unsolicited_ciev_vmwait1(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Voicemail 1 waiting: %s\n", pars);
}

static void handle_unsolicited_ciev_vmwait2(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Voicemail 2 waiting: %s\n", pars);
}

static void handle_unsolicited_ciev_ciphcall(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Ciphercall: %s\n", pars);
}

static void handle_unsolicited_ciev_eons(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Enhanced Operator Name String: %s\n", pars);
}

static void handle_unsolicited_ciev_nitz(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_me *me = to_me(urc->comm);

	vgsm_me_debug_state(me, "Network Identity and Time Zone: %s\n", pars);
}

static void handle_unsolicited_ciev(
	const struct vgsm_req *urc)
{
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CIEV '%s'\n", pars);
		return;
	}

	else if (!strcmp(field, "battchg"))
		handle_unsolicited_ciev_battchg(urc, pars_ptr);
	else if (!strcmp(field, "signal"))
		handle_unsolicited_ciev_signal(urc, pars_ptr);
	else if (!strcmp(field, "service"))
		handle_unsolicited_ciev_service(urc, pars_ptr);
	else if (!strcmp(field, "sounder"))
		handle_unsolicited_ciev_sounder(urc, pars_ptr);
	else if (!strcmp(field, "message"))
		handle_unsolicited_ciev_message(urc, pars_ptr);
	else if (!strcmp(field, "call"))
		handle_unsolicited_ciev_call(urc, pars_ptr);
	else if (!strcmp(field, "roam"))
		handle_unsolicited_ciev_roam(urc, pars_ptr);
	else if (!strcmp(field, "smsfull"))
		handle_unsolicited_ciev_smsfull(urc, pars_ptr);
	else if (!strcmp(field, "rssi"))
		handle_unsolicited_ciev_rssi(urc, pars_ptr);
	else if (!strcmp(field, "audio"))
		handle_unsolicited_ciev_audio(urc, pars_ptr);
	else if (!strcmp(field, "vmwait1"))
		handle_unsolicited_ciev_vmwait1(urc, pars_ptr);
	else if (!strcmp(field, "vmwait2"))
		handle_unsolicited_ciev_vmwait2(urc, pars_ptr);
	else if (!strcmp(field, "ciphcall"))
		handle_unsolicited_ciev_ciphcall(urc, pars_ptr);
	else if (!strcmp(field, "eons"))
		handle_unsolicited_ciev_eons(urc, pars_ptr);
	else if (!strcmp(field, "nitz"))
		handle_unsolicited_ciev_nitz(urc, pars_ptr);
	else
		ast_log(LOG_NOTICE, "Unhandled CIEV '%s'\n", field);
}

static void handle_unsolicited_cgev(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_cala(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sysstart(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	vgsm_me_debug_state(me, "ME started (^SYSSTART received)\n");

	vgsm_mesim_send_message(&me->mesim,
			VGSM_MESIM_MSG_ME_POWERED_ON,
				NULL, 0);

	vgsm_me_set_status(me,
		VGSM_ME_STATUS_WAITING_INITIALIZATION,
		WAITING_INITIALIZATION_DELAY, NULL);
}

static void handle_unsolicited_shutdown(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);

	vgsm_me_debug_state(me, "ME powered off (^SHUTDOWN received)\n");

	vgsm_me_set_status(me, VGSM_ME_STATUS_OFF, -1, NULL);
}

static void handle_unsolicited_slcc(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sals(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_scwa(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sis(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sisr(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sisw(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_smgo(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_scks(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&me->lock);
	if (atoi(pars)) {
		me->sim.inserted = TRUE;

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_WAITING_INITIALIZATION,
			WAITING_INITIALIZATION_SIM_INSERTED_DELAY,
			"SIM inserted");
	} else {
		me->sim.inserted = FALSE;

		if (me->status == VGSM_ME_STATUS_READY ||
		    me->status == VGSM_ME_STATUS_WAITING_PIN)
			vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_SIM, -1,
				"SIM removed");
	}
	ast_mutex_unlock(&me->lock);
}

static void handle_unsolicited_sbc(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_log(LOG_ERROR,
		"%s: Power supply: %s\n", pars, me->name);
}

static void handle_unsolicited_sstn(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sctm_a(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Battery is under critical low temperature limit"
			" and shutting down\n", me->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Battery is under lower temperature limit\n",
			me->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Battery temperature is now ok\n",
			me->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Battery is over high temperature limit\n",
			me->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Battery is over critical high temperature limit"
			" and shutting down\n", me->name);
	break;
	}
}

static void handle_unsolicited_sctm_b(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_me *me = to_me(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Engine is under critical low temperature limit"
			" and shutting down\n", me->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Engine is under lower temperature limit\n",
			me->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Engine temperature is now ok\n",
			me->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Engine is over high temperature limit\n",
			me->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Engine is over critical high temperature limit"
			" and shutting down\n", me->name);
	break;
	}
}

struct vgsm_urc_class vgsm_me_urcs[] =
{
	{ "NO CARRIER", handle_unsolicited_no_carrier, NULL },
	{ "NO DIALTONE", handle_unsolicited_no_dialtone, NULL },
	{ "BUSY", handle_unsolicited_busy, NULL },
	{ "RING: ", handle_unsolicited_ring, NULL },
	{ "+CRING: ", handle_unsolicited_cring, NULL },
	{ "+CREG: ", handle_unsolicited_creg, NULL },
	{ "+CUSD: ", handle_unsolicited_cusd, NULL },
	{ "+CCWA: ", handle_unsolicited_ccwa, NULL },
	{ "+CLIP: ", handle_unsolicited_clip, NULL },
	{ "+COLP: ", handle_unsolicited_colp, NULL },
	{ "+CCCM: ", handle_unsolicited_cccm, NULL },
	{ "+CSSI: ", handle_unsolicited_cssi, NULL },
	{ "+CSSU: ", handle_unsolicited_cssu, NULL },
	{ "+ALARM: ", handle_unsolicited_alarm, NULL },
	{ "+CGREG: ", handle_unsolicited_cgreg, NULL },
	{ "+CMTI: ", handle_unsolicited_cmti, NULL },
	{ "+CMT: ", handle_unsolicited_cmt, handle_cmt_end },
	{ "+CBM: ", handle_unsolicited_cbm, handle_cbm_end },
	{ "+CDS: ", handle_unsolicited_cds, handle_cds_end },
	{ "+CDSI: ", handle_unsolicited_cdsi, NULL },
	{ "+CIEV: ", handle_unsolicited_ciev, NULL },
	{ "+CGEV: ", handle_unsolicited_cgev, NULL },
	{ "+CALA: ", handle_unsolicited_cala, NULL },
	{ "^SYSSTART", handle_unsolicited_sysstart, NULL },
	{ "^SHUTDOWN", handle_unsolicited_shutdown, NULL },
	{ "^SLCC: ", handle_unsolicited_slcc, NULL },
	{ "^SALS: ", handle_unsolicited_sals, NULL },
	{ "^SCWA", handle_unsolicited_scwa, NULL },
	{ "^SIS: ", handle_unsolicited_sis, NULL },
	{ "^SISR: ", handle_unsolicited_sisr, NULL },
	{ "^SISW: ", handle_unsolicited_sisw, NULL },
	{ "^SMGO", handle_unsolicited_smgo, NULL },
	{ "^SCKS: ", handle_unsolicited_scks, NULL },
	{ "^SBC: ", handle_unsolicited_sbc, NULL },
	{ "^SSTN: ", handle_unsolicited_sstn, NULL },
	{ "^SCTM_A: ", handle_unsolicited_sctm_a, NULL },
	{ "^SCTM_B: ", handle_unsolicited_sctm_b, NULL },
	{ },
};

static int vgsm_me_pin_check_and_input(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	struct vgsm_req *req;
	const struct vgsm_req_line *first_line;
	int err;
	int res = 0;

	/* Be careful to not consume all the available attempts */
retry_spic:
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SPIC");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_SIM, -1,
				"SIM not present");
		vgsm_req_put(req);
		goto err_spic;
	} else if (err == CME_ERROR(262)) {
		vgsm_req_put(req);
		sleep(1);
		goto retry_spic;
	} else if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_spic;
	}

	me->sim.remaining_attempts =
		atoi(vgsm_req_first_line(req)->text + strlen("^SPIC: "));

	vgsm_req_put(req);

cpin_retry:
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_SIM, -1,
				"SIM not present");
		vgsm_req_put(req);
		goto err_spic;
	} else if (err == CME_ERROR(256)) {
		vgsm_req_put(req);
		sleep(1);
		goto cpin_retry;
	} else if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_spic;
	}

	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		/* Do nothing */
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		if (me->sim.remaining_attempts < 3) {
			vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_PIN, -1,
				"Input PIN manually");
			res = -1;
		} else if (strlen(mc->pin)) {
			int err = vgsm_req_make_wait_result(comm, 180 * SEC,
					"AT+CPIN=\"%s\"", mc->pin);

			if (err != VGSM_RESP_OK) {
				vgsm_me_set_status(me,
					VGSM_ME_STATUS_WAITING_PIN, -1,
					"SIM PIN refused (%s), input manually",
					vgsm_me_error_to_text(err));
				res = -1;
			}
		} else {
			vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_PIN, -1,
				"SIM PIN not configured, input manually");
			res = -1;
		}
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_PIN, -1,
				"SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_PIN, -1,
				"SIM requires PUK, input manually");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_PIN, -1,
				"SIM requires PUK2");
		res = -1;
	} else {
		vgsm_me_failed_text(me,
				"Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_req_put(req);

	return res;

err_spic:

	return -1;
}

static int vgsm_me_codec_init(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_codec_ctl cctl;

	cctl.parameter = VGSM_CODEC_RXGAIN;
	cctl.value = mc->rx_gain;

	if (ioctl(me->me_fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(IOC_CODEC_SET, RXGAIN) failed: %s\n",
			strerror(errno));

		goto err_ioctl_rxgain;
	}

	cctl.parameter = VGSM_CODEC_TXGAIN;
	cctl.value = mc->tx_gain;

	if (ioctl(me->me_fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(IOC_CODEC_SET, TXGAIN) failed: %s\n",
			strerror(errno));

		goto err_ioctl_txgain;
	}
/*
	} else if (!strcasecmp(parameter, "rxpre")) {
		cctl.parameter = VGSM_CODEC_TXPRE;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "txpre")) {
		cctl.parameter = VGSM_CODEC_TXPRE;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "dig_loop")) {
		cctl.parameter = VGSM_CODEC_DIG_LOOP;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "anal_loop")) {
		cctl.parameter = VGSM_CODEC_ANAL_LOOP;
		cctl.value = atoi(value);
	} else {
		fprintf(stderr, "Unknown parameter '%s'\n", parameter);
		return 1;
	}
*/
	return 0;

err_ioctl_txgain:
err_ioctl_rxgain:

	return -1;
}

static int vgsm_me_prepin_configure(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	int err;

	/* Enable call list unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC,
			"AT^SCFG=\"URC/CallStatus/CIEV\",\"verbose\"");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Configure AMR codec */
	err = vgsm_req_make_wait_result(comm, 5 * SEC,
			"AT^SCFG=\"Audio/AMR\",\"%s\"",
			mc->amr_enabled ? "enabled" : "disabled");
	if (err != VGSM_RESP_OK)
		goto err_no_req;
	
	/* Configure GSM codec */
	int csv_mode = 0;
	if (!mc->gsm_hr_enabled)
		csv_mode = 2;
	else if (mc->gsm_preferred == VGSM_CODEC_GSM_FR)
		csv_mode = 0;
	else if (mc->gsm_preferred == VGSM_CODEC_GSM_HR)
		csv_mode = 1;
	
	err = vgsm_req_make_wait_result(comm, 5 * SEC,
			"AT^SCFG=\"Call/SpeechVersion1\",\"%d\"",
			csv_mode);
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set TE character set */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC,
			"AT+CSCS=\"GSM\"");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Sets current time on me */
	if (mc->set_clock) {
		struct tm tm;
		time_t ct = time(NULL);

		ast_localtime(&ct, &tm, NULL);

		err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
			"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03ld\"",
			tm.tm_year % 100,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			-(timezone / 3600) + tm.tm_isdst);
		if (err != VGSM_RESP_OK) {
			vgsm_me_failed(me, err);
			goto err_no_req;
		}
	}

	/* Select audio mode 5 */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SNFS=5");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	if (me->interface_version == 1) {
		/* Select audio path 1 */
		err = vgsm_req_make_wait_result(comm, 10 * SEC,
							"AT^SAIC=2,1,1");
		if (err != VGSM_RESP_OK)
			goto err_no_req;
	} else {
		/* Select DAI audio */
		err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SAIC=1");
		if (err != VGSM_RESP_OK)
			goto err_no_req;
	}

	/* Set audio input */
	err = vgsm_req_make_wait_result(comm, 10 * SEC,
			"AT^SNFI=2,%d",
			mc->tx_calibrate);
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set audio output */
	err = vgsm_req_make_wait_result(comm, 10 * SEC,
			"AT^SNFO=1,0,0,0,0,%d,4,0",
			mc->rx_calibrate);
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set tones */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SNFPT=0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set ringer */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SRTC=0,0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited operating temperature notification */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT^SCTM=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited operating voltage notification */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT^SBC=0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Configure SYNC pin for LED usage */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT^SSYNC=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable SIM socket unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT^SCKS=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_me_failed(me, err);

	return -1;
}

static int vgsm_me_configure(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	int err;

	/* Disable cell broadcast to work around Siemens bugs */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSCB=0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Configure operator selection */
	switch(mc->operator_selection) {
	case VGSM_OPSEL_AUTOMATIC:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=0,2");
	break;

	case VGSM_OPSEL_MANUAL:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=1,2,%03u%02u",
			mc->operator_mcc,
			mc->operator_mnc);
	break;

	case VGSM_OPSEL_DEREGISTERED:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=2,2");
	break;

	case VGSM_OPSEL_MANUAL_FALLBACK:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=4,2,%03u%02u",
			mc->operator_mcc,
			mc->operator_mnc);
	break;

	default:
		assert(0);
		return 0;
	}

	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited supplementary service notification */
	err = vgsm_req_make_wait_result(comm, 20 * SEC, "AT+CSSN=1,1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set SMS Command Configuration */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT^SSCONF=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set SMS Display Availability */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT^SSDA=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set SMS mode to PDU */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CMGF=0");
	if (err != VGSM_RESP_OK)
		vgsm_me_failed(me, err);

	/* Select message service */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSMS=1");
	if (err != VGSM_RESP_OK)
		vgsm_me_failed(me, err);

	/************ We now enable unsolicited responses ************/

	/* Enable unsolicited registration informations */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CREG=2");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited unstructured supplementary data */
	err = vgsm_req_make_wait_result(comm, 180 * SEC, "AT+CUSD=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited GPRS registration status */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CGREG=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited GPRS event reporting */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CGEREP=2,1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable Called Line Presentation */
	err = vgsm_req_make_wait_result(comm, 180 * SEC, "AT+COLP=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable Calling Line Presentation */
	err = vgsm_req_make_wait_result(comm, 180 * SEC, "AT+CLIP=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable extended cellular result codes */
	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC, "AT+CRC=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable mobile equipment event reporting */
	err = vgsm_req_make_wait_result(comm,
		200 * MILLISEC, "AT+CMER=2,0,0,2,0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable call statistics after hangup */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "ATS18=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Use M20 compatible mode */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT^SM20=0,0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable call waiting unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CCWA=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Disable call list unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT^SLCC=0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Save the current configuration */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT&W");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_me_failed(me, err);

	return -1;
}

static int vgsm_me_postponed_configuration(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	int err;

	/* Enable unsolicited new message indications */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CNMI=2,2,2,1,1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Subscribe to all cell broadcast channels */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSCB=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_me_failed(me, err);

	return -1;
}

static int vgsm_me_update_static_info(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	struct vgsm_req *req;
	int err;

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->me.vendor,
		vgsm_req_first_line(req)->text,
		sizeof(me->me.vendor));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMM");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->me.model,
		vgsm_req_first_line(req)->text,
		sizeof(me->me.model));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMR");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->me.version,
		vgsm_req_first_line(req)->text,
		sizeof(me->me.version));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CGSN");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->me.imei,
		vgsm_req_first_line(req)->text,
		sizeof(me->me.imei));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);
	
	/*--------*/
	req = vgsm_req_make_wait(comm, 100 * MILLISEC, "AT^SCKS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	if (strlen(vgsm_req_first_line(req)->text) > 7) {
		const char *pars_ptr = vgsm_req_first_line(req)->text + 7;
		char field[32];

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SCKS '%s'\n",
				vgsm_req_first_line(req)->text);

			vgsm_req_put(req);
			goto no_sim;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SCKS '%s'\n",
				vgsm_req_first_line(req)->text);

			vgsm_req_put(req);
			goto no_sim;
		}

		ast_mutex_lock(&me->lock);
		if (atoi(field) == 1)
			me->sim.inserted = TRUE;
		else
			me->sim.inserted = FALSE;
		ast_mutex_unlock(&me->lock);
	}

	vgsm_req_put(req);

	if (!me->sim.inserted)
		goto no_sim;
	
	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CIMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->sim.imsi,
		vgsm_req_first_line(req)->text,
		sizeof(me->sim.imsi));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CXXCID");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	if (strlen(vgsm_req_first_line(req)->text) < strlen("+CXXCID: ")) {
		vgsm_req_put(req);
		goto err_failed;
	}

	ast_mutex_lock(&me->lock);
	strncpy(me->sim.card_id,
		vgsm_req_first_line(req)->text + strlen("+CXXCID: "),
		sizeof(me->sim.card_id));
	ast_mutex_unlock(&me->lock);

	vgsm_req_put(req);

	/*--------*/
retry_csca:
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CSCA?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		if (err == CME_ERROR(14)) { // SIM busy
			vgsm_req_put(req);
			sleep(1);
			goto retry_csca;
		}

		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		goto err_failed;
	}

	if (strlen(vgsm_req_first_line(req)->text) > strlen("+CSCA: ")) {
		const char *pars_ptr = vgsm_req_first_line(req)->text +
					strlen("+CSCA: ");
		char field[32];

		ast_mutex_lock(&me->lock);
		get_token(&pars_ptr, me->sim.smcc_address.digits,
			sizeof(me->sim.smcc_address.digits));

		if (me->sim.smcc_address.digits[0] == '+')
			memmove(me->sim.smcc_address.digits,
				me->sim.smcc_address.digits + 1,
				strlen(me->sim.smcc_address.digits));

		if (get_token(&pars_ptr, field, sizeof(field))) {
			me->sim.smcc_address.ton =
					(atoi(field) & 0x70) >> 4;
			me->sim.smcc_address.np =
					atoi(field) & 0x0f;
		}
		ast_mutex_unlock(&me->lock);
	}

	vgsm_req_put(req);

no_sim:

	return 0;

err_failed:

	return -1;
}

static int vgsm_me_update_net_info(
	struct vgsm_me *me)
{
	int err;
	struct vgsm_comm *comm = &me->comm;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CREG?");
	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		vgsm_me_failed(me, res);
		vgsm_req_put(req);
		err = -EIO;
		goto err_creg_read_response;
	}

	const char *line = vgsm_req_first_line(req)->text;

	if (strlen(line) > strlen("+CREG: "))
		vgsm_update_me_by_creg(me,
				line + strlen("+CREG: "), TRUE);

	vgsm_req_put(req);

	if (me->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    me->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {

		err = vgsm_update_smond(me);
		if (err < 0)
			goto err_update_smond;

		err = vgsm_update_cops(me);
		if (err < 0)
			goto err_update_cops;
	}

	return 0;

err_update_cops:
err_update_smond:
err_creg_read_response:

	return err;
}

/***********************************************/

static int vgsm_me_init_at_interface(
	struct vgsm_me *me,
	struct vgsm_me_config *mc)
{
	struct vgsm_comm *comm = &me->comm;
	int err;

	int flow_control;
	switch(me->flow_control) {
	case VGSM_FLOW_NONE: flow_control = 0; break;
	case VGSM_FLOW_SW: flow_control = 1; break;
	case VGSM_FLOW_HW: flow_control = 3; break;
	default: assert(0); break;
	}

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT Z0 E1 V1 Q0 \\Q%d",
		flow_control);
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT+IPR=%d",
		me->interface_version == 1 ? 38400 : 230400);
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT+CMEE=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_me_failed(me, err);

	return -1;
}

static int vgsm_me_open(
	struct vgsm_me *me)
{
	int err;

	assert(me->me_fd == -1);

	ast_mutex_lock(&me->lock);
	struct vgsm_me_config *mc;
	mc = vgsm_me_config_get(me->current_config);
	ast_mutex_unlock(&me->lock);

	me->me_fd = open(mc->device_filename, O_RDWR | O_NOCTTY | O_NDELAY);
	if (me->me_fd < 0) {
		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Error opening device: open(%s): %s",
				mc->device_filename,
				strerror(errno));

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_CLOSED, FAILED_RETRY_TIME,
			tmpstr);

		err = -errno;
		goto err_me_open;
	}

	int flags;
	flags = fcntl(me->me_fd, F_GETFL, 0);

	flags &= ~O_NONBLOCK;

	if (fcntl(me->me_fd, F_SETFL, flags) < 0) {
		ast_log(LOG_ERROR,
			"Cannot set ME fd to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_me_fcntl;
	}

	if (ioctl(me->me_fd, VGSM_IOC_GET_INTERFACE_VERSION,
			&me->interface_version) < 0) {
		me->interface_version = 1;
	}

	if (tcflush(me->me_fd, TCIOFLUSH) < 0) {
		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Error flushing device: %s",
				strerror(errno));

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_CLOSED, FAILED_RETRY_TIME,
			tmpstr);

		err = -errno;
		goto err_me_flush;
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio));

	newtio.c_cflag = CS8 | CREAD | HUPCL;

        if (me->interface_version == 1)
		newtio.c_cflag |= B38400;
	else
		newtio.c_cflag |= B230400;

	if (mc->flow_control == VGSM_FLOW_AUTO) {
		if (me->interface_version == 1)
			me->flow_control = VGSM_FLOW_SW;
		else
			me->flow_control = VGSM_FLOW_HW;
	} else
		me->flow_control = mc->flow_control;

	if (me->flow_control == VGSM_FLOW_NONE) {
		newtio.c_cflag |= CLOCAL;
		newtio.c_iflag = 0;
	} else if (me->flow_control == VGSM_FLOW_SW) {
		newtio.c_cflag |= CLOCAL;
		newtio.c_iflag = IXON | IXOFF;
	} else if (me->flow_control == VGSM_FLOW_HW) {
		newtio.c_cflag |= CRTSCTS;
		newtio.c_iflag = 0;
	}

	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	
	newtio.c_cc[VINTR]	= 0;
	newtio.c_cc[VQUIT]	= 0;
	newtio.c_cc[VERASE]	= 0;
	newtio.c_cc[VKILL]	= 0;
	newtio.c_cc[VEOF]	= 4;
	newtio.c_cc[VTIME]	= 0;
	newtio.c_cc[VMIN]	= 1;
	newtio.c_cc[VSWTC]	= 0;
	newtio.c_cc[VSTART]	= 0;
	newtio.c_cc[VSTOP]	= 0;
	newtio.c_cc[VSUSP]	= 0;
	newtio.c_cc[VEOL]	= 0;
	newtio.c_cc[VREPRINT]	= 0;
	newtio.c_cc[VDISCARD]	= 0;
	newtio.c_cc[VWERASE]	= 0;
	newtio.c_cc[VLNEXT]	= 0;
	newtio.c_cc[VEOL2]	= 0;
	
	if (tcsetattr(me->me_fd, TCSANOW, &newtio) < 0) {
		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Error setting tty's attributes: tcsetattr(%s): %s",
				mc->device_filename,
				strerror(errno));

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_CLOSED, FAILED_RETRY_TIME,
			tmpstr);

		err = -errno;
		goto err_tcsetattr;
	}

	/***************/

	if (me->interface_version == 2) {

		struct vgsm_fw_version fw_version;
		if (ioctl(me->me_fd, VGSM_IOC_FW_VERSION,
							&fw_version) < 0) {
			ast_log(LOG_NOTICE,
				"%s: cannot get firmware version\n",
				me->name);
		} else {
			me->card.ver_maj = fw_version.maj;
			me->card.ver_min = fw_version.min;
			me->card.ver_ser = fw_version.ser;
		}

		if (ioctl(me->me_fd, VGSM_IOC_READ_SERIAL,
					&me->card.serial) < 0) {
			ast_log(LOG_NOTICE,
				"%s: cannot read card's serial number\n",
				me->name);
		}

		if (ioctl(me->me_fd, VGSM_IOC_GET_ID, &me->id) < 0) {
			ast_log(LOG_ERROR,
				"%s: cannot read ID\n",
				me->name);
		}

		if (ioctl(me->me_fd, VGSM_IOC_CARD_GET_ID,
					&me->card.id) < 0) {
			ast_log(LOG_ERROR,
				"%s: cannot read card's ID\n",
				me->name);
		}

		vgsm_mesim_create(&me->mesim, me, me->name);

		err = vgsm_mesim_open(&me->mesim, mc->mesim_device_filename);
		if (err < 0) {
			vgsm_me_failed_text(me,
				"Error opening ME-SIM device: %s",
				strerror(errno));

			goto err_mesim_open;
		}

		struct vgsm_mesim_set_mode sm = {
			.driver_type = mc->sim_driver_type,
		};

		strncpy(sm.device_filename, mc->sim_device_filename,
					sizeof(sm.device_filename));
		memcpy(&sm.client_addr, &mc->sim_client_addr,
					sizeof(sm.client_addr));

		vgsm_mesim_send_message(&me->mesim,
				VGSM_MESIM_MSG_SET_MODE,
				&sm, sizeof(sm));
	}

	/***************/

	int val;
	if (ioctl(me->me_fd, VGSM_IOC_POWER_GET, &val) < 0) {
		vgsm_me_failed_text(me,
			"Error getting power status:"
			" ioctl(POWER_GET): %s",
			strerror(errno));

		err = -errno;
		goto err_ioctl_power_get;
	}

	err = vgsm_comm_open(&me->comm, me->me_fd, me->name);
	if (err < 0) {
		vgsm_me_failed_text(me,
			"Error opening communication port: %s",
			strerror(err));

		goto err_comm_open;
	}

	if (val) {
		vgsm_comm_send_message(&me->comm,
				VGSM_COMM_MSG_INITIALIZE, NULL, 0);

		vgsm_me_debug_state(me,
			"ME is already powered on, I'm not waiting"
			" for SYSSTART\n");

		vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_INITIALIZATION,
				0, NULL);
	} else {
		vgsm_me_ignite(me);

		/* The very first me power-up will not send ^SYSSTART URC
		 * because it is configured in auto-bauding mode. We will time
		 * out and initialize it anyway.
		 */
		me->power_attempts = 0;
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_POWERING_ON,
				POWERING_ON_TIMEOUT,
				NULL);
	}

	vgsm_me_config_put(mc);

	return 0;

	vgsm_comm_close(&me->comm);
err_comm_open:
err_ioctl_power_get:
	vgsm_mesim_close(&me->mesim);
err_mesim_open:
err_tcsetattr:
err_me_flush:
err_me_fcntl:
	close(me->me_fd);
	me->me_fd = -1;
err_me_open:
	vgsm_me_config_put(mc);

	return err; 
}

static void vgsm_me_initialize(
	struct vgsm_me *me)
{
	vgsm_me_debug_state(me, "ME initializing...\n");

	ast_mutex_lock(&me->lock);
	struct vgsm_me_config *mc;
	mc = vgsm_me_config_get(me->current_config);
	ast_mutex_unlock(&me->lock);

	vgsm_me_set_status(me,
		VGSM_ME_STATUS_INITIALIZING, -1,
		NULL);

	if (me->interface_version == 1) {
		if (vgsm_me_codec_init(me, mc) < 0) {
			vgsm_me_failed_text(me,
					"Error configuring CODEC");

			goto initialization_failed;
		}
	}

	int val;
	if (ioctl(me->me_fd, VGSM_IOC_POWER_GET, &val) < 0) {
		vgsm_me_failed_text(me,
			"%s: Error getting power status: ioctl(POWER_GET): %s",
			me->name,
			strerror(errno));

		goto initialization_failed;
	}

	if (!val) {
		ast_log(LOG_NOTICE,
			"%s: is not powered on, re-igniting\n",
			me->name);

		me->power_attempts = 0;
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_POWERING_ON,
				POWERING_ON_TIMEOUT,
				"Power lost");

		vgsm_me_ignite(me);

		goto initialization_failed;
	}

	if (vgsm_me_init_at_interface(me, mc) < 0)
		goto initialization_failed;

	if (vgsm_me_prepin_configure(me, mc) < 0)
		goto initialization_failed;

	if (vgsm_me_pin_check_and_input(me, mc) < 0)
		goto initialization_failed;

	if (vgsm_me_configure(me, mc) < 0)
		goto initialization_failed;

	if (vgsm_me_update_static_info(me, mc) < 0)
		goto initialization_failed;

	if (vgsm_me_update_net_info(me) < 0)
		goto initialization_failed;

	if (vgsm_me_postponed_configuration(me, mc) < 0)
		goto initialization_failed;

	me->failure_attempts = 0;

	if (me->in_service) {
		vgsm_me_set_status(me,
			VGSM_ME_STATUS_READY,
			READY_UPDATE_TIME,
			" ");
	} else {
		vgsm_me_set_status(me,
			VGSM_ME_STATUS_OFFLINE,
			READY_UPDATE_TIME,
			" ");
	}

	vgsm_me_debug_state(me, "me successfully initialized\n");

	vgsm_me_config_put(mc);

	return;

initialization_failed:

	vgsm_me_config_put(mc);

	/* If no-one changed the status to something significant fall back to
	 * FAILED
	 */

	if (me->status == VGSM_ME_STATUS_INITIALIZING)
		vgsm_me_failed_text(me, "");
}

static void vgsm_me_timer(void *data)
{
	struct vgsm_me *me = data;

	switch(me->status) {
	case VGSM_ME_STATUS_CLOSED:
		vgsm_me_open(me);
	break;

	case VGSM_ME_STATUS_RESETTING:
	case VGSM_ME_STATUS_POWERING_ON: {
		int val;
		if (ioctl(me->me_fd, VGSM_IOC_POWER_GET, &val) < 0) {
			vgsm_me_failed_text(me,
				"Error getting power status:"
				" ioctl(POWER_GET): %s",
				strerror(errno));

			return;
		}

		if (val) {
			vgsm_me_debug_state(me, "SYSSTART missed\n");

			vgsm_mesim_send_message(&me->mesim,
					VGSM_MESIM_MSG_ME_POWERED_ON,
					NULL, 0);

			vgsm_me_set_status(me,
				VGSM_ME_STATUS_WAITING_INITIALIZATION,
				0, NULL);
		} else {
			me->power_attempts++;
			if (me->power_attempts > 3) {
				ast_log(LOG_ERROR,
					"%s: Power-on permanently failed\n",
					me->name);

				vgsm_me_set_status(me,
					VGSM_ME_STATUS_OFF, -1,
					"Power-on sequence failure");
			} else {
				ast_log(LOG_NOTICE,
					"%s: Power-on sequence failed,"
					" retrying\n",
					me->name);

				vgsm_me_set_status(me,
					VGSM_ME_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT,
					"Power-on sequence failed");

				vgsm_me_ignite(me);
			}
		}
	}
	break;

	case VGSM_ME_STATUS_POWERING_OFF:
		vgsm_me_emerg_off(me);

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_OFF, -1,
			" ");
	break;

	case VGSM_ME_STATUS_FAILED:

		if (me->failure_attempts < 3) {
			vgsm_mesim_close(&me->mesim);

			vgsm_comm_close(&me->comm);

			if (tcflush(me->me_fd, TCIOFLUSH) < 0) {
				ast_log(LOG_WARNING,
					"Error flushing device: %s",
					strerror(errno));
			}

			close(me->me_fd);
			me->me_fd = -1;

			vgsm_me_set_status(me,
				VGSM_ME_STATUS_CLOSED,
				1 * SEC,
				NULL);
		} else {
			me->failure_attempts = 0;

			ast_log(LOG_NOTICE, "%s: Power-cycling me\n",
				me->name);

			vgsm_me_emerg_off(me);

			vgsm_me_set_status(me,
				VGSM_ME_STATUS_POWERING_ON,
				POWERING_ON_TIMEOUT,
				NULL);

			vgsm_me_ignite(me);
		}
	break;

	case VGSM_ME_STATUS_WAITING_INITIALIZATION:
		vgsm_me_initialize(me);
	break;

	case VGSM_ME_STATUS_READY:
		if (vgsm_me_update_net_info(me) >= 0) {
			/* Re-arm timer */
			vgsm_me_set_status(me,
				VGSM_ME_STATUS_READY,
				READY_UPDATE_TIME,
				" ");
		}
	break;

	case VGSM_ME_STATUS_INITIALIZING:
	case VGSM_ME_STATUS_OFF:
	case VGSM_ME_STATUS_UNCONFIGURED:
	case VGSM_ME_STATUS_WAITING_SIM:
	case VGSM_ME_STATUS_WAITING_PIN:
	case VGSM_ME_STATUS_OFFLINE:
		ast_log(LOG_ERROR,
			"vgsm: ME '%s': Unexpected timer in status %s\n",
			me->name,
			vgsm_me_status_to_text(me->status));

	break;
	}
}

static void *vgsm_me_monitor_thread_main(void *data)
{
	struct vgsm_me *me = (struct vgsm_me *)data;

	for(;;) {
		vgsm_timerset_run(&me->timerset);

		longtime_t timeout = vgsm_timerset_next(&me->timerset);

		if (vgsm.debug_timer)
			ast_verbose(
				"vgsm: me handler sleeping for %lld ms\n",
				timeout / 1000);

		if (timeout > 100000000)
			sleep(timeout / 1000000);
		else if (timeout >= 0)
			usleep(timeout);
		else
			sleep(3600);
	}

	return NULL;
}

void vgsm_me_shutdown_all(void)
{
	struct vgsm_me *me;

	ast_verbose("vgsm: powering off all MEs\n");

	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	list_for_each_entry(me, &vgsm.mes_list, node) {

		ast_mutex_lock(&me->lock);

		/* Check me->status != UNCONFIGURED before accessing
		 * me->current_config
		 */

		if (me->status != VGSM_ME_STATUS_UNCONFIGURED &&
		    me->status != VGSM_ME_STATUS_CLOSED &&
		    me->status != VGSM_ME_STATUS_POWERING_OFF &&
		    me->status != VGSM_ME_STATUS_OFF &&
		    me->current_config->poweroff_on_exit &&
		    me->comm.fd >= 0) {
			vgsm_me_set_status(me,
					VGSM_ME_STATUS_POWERING_OFF,
					POWERING_OFF_TIMEOUT,
					"Asterisk shutdown");

			_vgsm_req_put(vgsm_req_make(&me->comm,
					3 * SEC, "AT^SMSO"));
		}

		ast_mutex_unlock(&me->lock);
	}
	ast_rwlock_unlock(&vgsm.mes_list_lock);

	time_t begin = time(NULL);

	int all_off = TRUE;
	while(time(NULL) - begin < 10) {

		all_off = TRUE;
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		list_for_each_entry(me, &vgsm.mes_list, node) {

			ast_mutex_lock(&me->lock);
			if (me->status != VGSM_ME_STATUS_OFF &&
			    me->status != VGSM_ME_STATUS_CLOSED &&
			    me->status != VGSM_ME_STATUS_UNCONFIGURED &&
			    me->current_config->poweroff_on_exit)
				all_off = FALSE;

			ast_mutex_unlock(&me->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);

		if (all_off)
			break;

		sleep(1);
	}

	if (!all_off)
		ast_verbose("vgsm: Failure to power off all MEs\n");

	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	list_for_each_entry(me, &vgsm.mes_list, node) {

		ast_mutex_lock(&me->lock);
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_CLOSED,
				-1, "Shutdown");
		vgsm_comm_close(&me->comm);
		close(me->me_fd);
		me->me_fd = -1;

		if (me->interface_version == 2)
			vgsm_mesim_close(&me->mesim);

		ast_mutex_unlock(&me->lock);
	}
	ast_rwlock_unlock(&vgsm.mes_list_lock);
}

/*---------------------------------------------------------------------------*/

static void vgsm_me_show_me(int fd, struct vgsm_me *me)
{
	struct vgsm_req *req;

	ast_mutex_lock(&me->lock);
	struct vgsm_me_config *mc = me->current_config;

	ast_cli(fd,
		"\n"
		"  ME: %s\n"
		"  Status: %s\n"
		"  In service: %s\n",
		me->name,
		vgsm_me_status_to_text(me->status),
		me->in_service ? "YES" : "NO");

	if (me->status_reason && strlen(me->status_reason))
		ast_cli(fd, "  Reason: %s\n", me->status_reason);

	if (me->failure_count)
		ast_cli(fd, "\n  Failure count: %d\n", me->failure_count);

	if (me->status == VGSM_ME_STATUS_UNCONFIGURED)
		goto out;

	ast_cli(fd,
		"\n"
		"  Device: %s\n"
		"  ME SIM Device: %s\n"
		"  Context: %s\n"
		"  Set clock: %s\n"
		"  Power off on exit: %s\n",
		mc->device_filename,
		mc->mesim_device_filename,
		mc->context,
		mc->set_clock ? "YES" : "NO",
		mc->poweroff_on_exit ? "YES" : "NO");

	ast_cli(fd,
		"  SMS sender domain: %s\n"
		"  SMS recipient address: %s\n"
		"\n"
		"  DTMF quelch: %s\n"
		"  DTMF mutemax: %s\n"
		"  DTMF relax: %s\n"
		"\n"
		"  AMR enabled: %s\n"
		"  GSM-HR enabled: %s\n"
		"  GSM preferred CODEC: %s\n"
		"\n"
		"  Jitter buffer average: %d\n"
		"  Jitter buffer maxhole: %d\n"
		"  Jitter buffer low-mark: %d\n"
		"  Jitter buffer hard low-mark: %d\n"
		"  Jitter buffer high-mark: %d\n"
		"  Jitter buffer hard high-mark: %d\n",
		mc->sms_sender_domain,
		mc->sms_recipient_address,
		mc->dtmf_quelch ? "YES" : "NO",
		mc->dtmf_mutemax ? "YES" : "NO",
		mc->dtmf_relax ? "YES" : "NO",
		mc->amr_enabled ? "YES" : "NO",
		mc->gsm_hr_enabled ? "YES" : "NO",
		vgsm_codec_to_text(mc->gsm_preferred),
		mc->jitbuf_average,
		mc->jitbuf_maxhole,
		mc->jitbuf_low,
		mc->jitbuf_hardlow,
		mc->jitbuf_high,
		mc->jitbuf_hardhigh);

	if (me->interface_version == 1) {
		ast_cli(fd,
			"  RX-gain: %d\n"
			"  TX-gain: %d\n",
			mc->rx_gain,
			mc->tx_gain);
	}


/*	if (mc->mesim.sim_holder == VGSM_SIM_ROUTE_EXTERNAL)
		ast_cli(fd, "  Route to sim: external\n");
	else if (mc->mesim.sim_holder == VGSM_SIM_ROUTE_DEFAULT)
		ast_cli(fd, "  Route to sim: default\n");
	else
		ast_cli(fd, "  Route to sim: %s\n", mc->mesim.sim_holder->name);
*/

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_OFFLINE)
		goto out;

	/* Avoid holding a lock during long term operations */
	ast_mutex_unlock(&me->lock);

	/* Voltage */
	int voltage = -1;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, "AT^SBV");
	if (vgsm_req_status(req) == VGSM_RESP_OK) {
		const char *line = vgsm_req_first_line(req)->text;

		if (strlen(line) > strlen("^SBV: "))
			voltage = atoi(line + strlen("^SBV: "));
	}
	vgsm_req_put(req);

	/* Current */
	int current = -1;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, "AT^SBC?");
	if (vgsm_req_status(req) == VGSM_RESP_OK) {
		const char *line = vgsm_req_first_line(req)->text;
		const char *pars_ptr = line + strlen("^SBC: ");
		char field[32];

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
			goto err_sbc;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
			goto err_sbc;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
			goto err_sbc;
		}

		current = atoi(field);
err_sbc:;
	}
	vgsm_req_put(req);

	ast_mutex_lock(&me->lock);

	ast_cli(fd,
		"\n"
		"  Model: %s %s\n"
		"  Version: %s\n"
		"  IMEI: %s\n",
		me->me.vendor,
		me->me.model,
		me->me.version,
		me->me.imei);

	if (voltage != -1)
		ast_cli(fd, "  Supply voltage: %d mV\n", voltage);

	if (current != -1)
		ast_cli(fd, "  Supply current: %d mA\n", current);

	if (me->card.ver_maj) {
		ast_cli(fd,
			"\n"
			"  Card's firmware: %d.%d.%d\n",
			me->card.ver_maj,
			me->card.ver_min,
			me->card.ver_ser);

		if ((me->card.ver_maj << 16 |
		     me->card.ver_min << 8 |
		     me->card.ver_ser) < VGSM_MINIMUM_FIRMWARE) {
			ast_cli(fd,
				"  !!! WARNING, firmware upgrade to "
				"%d.%d.%d is required !!!\n",
				(VGSM_MINIMUM_FIRMWARE & 0xff0000) >> 16,
				(VGSM_MINIMUM_FIRMWARE & 0x00ff00) >> 8,
				(VGSM_MINIMUM_FIRMWARE & 0x0000ff) >> 0);
		}
	}

	if (me->card.serial != 0xffffffff &&
	    me->card.serial != 0x00000000) {
		ast_cli(fd,
			"  Card's S/N: %012d\n",
			me->card.serial);
	}

out:
	ast_cli(fd, "\n");

	ast_mutex_unlock(&me->lock);
}

static void vgsm_print_call_class(int fd, int cls)
{
	if (cls & 1)
		ast_cli(fd, "voice, ");

	if (cls & 2)
		ast_cli(fd, "data, ");

	if (cls & 4)
		ast_cli(fd, "fax, ");

	if (cls & 8)
		ast_cli(fd, "SMS, ");

	if (cls & 16)
		ast_cli(fd, "data circuit sync, ");

	if (cls & 32)
		ast_cli(fd, "data circuit async, ");

	if (cls & 64)
		ast_cli(fd, "dedicated packet access, ");

	if (cls & 128)
		ast_cli(fd, "dedicated PAD access, ");
}

static int vgsm_me_show_forwarding_entry(
	int fd, const char *name, const char *line)
{
	const char *pars_ptr = line;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_cli(fd, "Error parsing CCEF reply '%s'\n", line);
		return RESULT_FAILURE;
	}

	int status = atoi(field);

	if (status == 1) {
		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_cli(fd, "Error parsing CCEF reply '%s'\n", line);
			return RESULT_FAILURE;
		}

		int cls = atoi(field);

		ast_cli(fd, "Forward: %s, ", name);

		vgsm_print_call_class(fd, cls);

		if (get_token(&pars_ptr, field, sizeof(field))) {

			ast_cli(fd, "traffic to %s", field);

			if (get_token(&pars_ptr, field, sizeof(field))) {
				if (get_token(&pars_ptr, field,
							sizeof(field))) {
					ast_cli(fd, " (delay %ds)",
						atoi(field));
				}
			}
		}

		ast_cli(fd, "\n");
	}

	return RESULT_SUCCESS;
}

static const char *vgsm_me_forwarding_reason_to_text(int value)
{
	switch(value) {
	case 0:
		return "unconditional";
	case 1:
		return "when busy";
	case 2:
		return "when not replying";
	case 3:
		return "when not reachable";
	}

	return "*INVALID*";
}

static int vgsm_me_show_forwarding(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_OFFLINE) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		goto out;
	}

	ast_mutex_unlock(&me->lock);

	int i;
	for(i=0; i<=3; i++) {
		struct vgsm_req *req;
		req = vgsm_req_make_wait(&me->comm, 180 * SEC,
						"AT+CCFC=%d,2", i);

		if (!req)
			return RESULT_FAILURE;

		int res = vgsm_req_status(req);
		if (res != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_me_error_to_text(res),
				res);

			vgsm_req_put(req);

			return RESULT_FAILURE;
		}

		struct vgsm_req_line *line;
		list_for_each_entry(line, &req->lines, node) {
			if (!strncmp(line->text, "+CCFC: ",
						strlen("+CCFC: ")))
				vgsm_me_show_forwarding_entry(fd,
					vgsm_me_forwarding_reason_to_text(i),
					line->text + strlen("+CCFC: "));
		}

		vgsm_req_put(req);
	}

	ast_cli(fd, "\n");

out:

	return RESULT_SUCCESS;
}

static int vgsm_me_show_callwaiting_entry(
	int fd, const char *line)
{
	const char *pars_ptr = line;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_cli(fd, "Error parsing CCWA reply '%s'\n", line);
		return RESULT_FAILURE;
	}

	int status = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_cli(fd, "Error parsing CCWA reply '%s'\n", line);
		return RESULT_FAILURE;
	}

	int cls = atoi(field);

	ast_cli(fd, "Call waiting: ");
	vgsm_print_call_class(fd, cls);
	ast_cli(fd, " %s\n", status == 1 ? "Enabled" : "Disabled");

	return RESULT_SUCCESS;
}

static int vgsm_me_show_callwaiting(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_OFFLINE) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		return RESULT_FAILURE;
	}

	ast_mutex_unlock(&me->lock);

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 180 * SEC, "AT+CCWA=1,2");
	int res = vgsm_req_status(req);

	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);

		vgsm_req_put(req);

		return RESULT_FAILURE;
	}

	ast_cli(fd, "Call waiting:\n\n");

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node) {
		if (!strncmp(line->text, "+CCWA: ", strlen("+CCWA: ")))
			vgsm_me_show_callwaiting_entry(fd,
				line->text + strlen("+CCWA: "));
	}

	vgsm_req_put(req);

	ast_cli(fd, "\n");

	return RESULT_SUCCESS;
}

static int vgsm_me_show_sim(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_WAITING_SIM &&
	    me->status != VGSM_ME_STATUS_WAITING_PIN &&
	    me->status != VGSM_ME_STATUS_OFFLINE) {
		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		goto out;
	}

	ast_cli(fd, "ME SIM:\n"
		"  VCC=%d, RST=%d\n"
		"  State: %s\n",
		me->mesim.vcc,
		me->mesim.rst,
		vgsm_mesim_state_to_text(me->mesim.state));

	if (me->mesim.driver->cli_show)
		me->mesim.driver->cli_show(me->mesim.driver, fd);

	if (!me->sim.inserted) {
		ast_cli(fd,
			"\nSIM:\n"
			"  Not inserted\n");
		goto out;
	}

	ast_cli(fd,
		"\nSIM:\n"
		"  Card ID: %s\n"
		"  IMSI: %s\n"
		"  PIN remaining attempts: %d\n"
		"  Services center: %s/%s/%s%s\n",
		me->sim.card_id,
		me->sim.imsi,
		me->sim.remaining_attempts,
		vgsm_numbering_plan_to_text(me->sim.smcc_address.np),
		vgsm_type_of_number_to_text(me->sim.smcc_address.ton),
		vgsm_number_prefix(&me->sim.smcc_address),
		me->sim.smcc_address.digits);

	ast_cli(fd, "\n");

#if 0
	struct vgsm_sim *sim = me->sim.sim;
	if (!sim)
		goto out;

	struct vgsm_sim_file *file;
	__u8 *buf;
	int i;
	int err;

	/***************/

	file = vgsm_sim_file_alloc(sim);
	if (!file)
		goto out;
	vgsm_sim_file_open(file, 0x6F46);

	buf = alloca(file->length + 1);
	vgsm_sim_file_read(file, buf, 0, file->length);
	buf[file->length] = '\0';
	ast_cli(fd, "  Operator name: %s\n", buf);
	vgsm_sim_file_put(file);

	/***************/

	file = vgsm_sim_file_alloc(sim);
	if (!file)
		goto out;

	__u8 phase;
	vgsm_sim_file_open(file, 0x6FAE);
	err = vgsm_sim_file_read(file, &phase, 0, 1);
	if (err < 0) {
		ast_cli(fd, "Cannot read SIM file: %s\n", strerror(-err));
		goto out;
	}

	if (phase == 3)
		ast_cli(fd, "  Phase: 2 with profile download\n");
	else
		ast_cli(fd, "  Phase: %d\n", phase);

	vgsm_sim_file_put(file);

	/***************/

	file = vgsm_sim_file_alloc(sim);
	if (!file)
		goto out;
	vgsm_sim_file_open(file, 0x6F30);

	buf = alloca(file->length);

	err = vgsm_sim_file_read(file, buf, 0, file->length);
	if (err < 0) {
		ast_cli(fd, "Cannot read SIM file: %s\n", strerror(-err));
		goto out;
	}

	ast_cli(fd, "\nPLMN Selector:\n");
	for(i=0; i<file->length; i+=3) {

		if (buf[i] == 0xff &&
		    buf[i + 1] == 0xff &&
		    buf[i + 2] == 0xff)
			continue;

		__u16 mcc = (buf[i] & 0x0f) * 100 +
			    ((buf[i] & 0xf0) >> 4) * 10 +
			    (buf[i + 1] & 0x0f);

		__u16 mnc = (buf[i + 2] & 0x0f) * 10+
			    ((buf[i + 2] & 0xf0) >> 4);

		if ((buf[i + 1] & 0xf0) != 0xf0)
			    mnc += ((buf[i + 1] & 0xf0) >> 4) * 100;

		ast_cli(fd, "  %03d%02d", mcc, mnc);

		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(mcc, mnc);
		if (op_info)
			ast_cli(fd, " %s - %s",
				op_info->name,
				op_info->country ? op_info->country->name : "");

		ast_cli(fd, "\n");
	}

	vgsm_sim_file_put(file);

	/***************/

	file = vgsm_sim_file_alloc(sim);
	if (!file)
		goto out;
	vgsm_sim_file_open(file, 0x6F7B);

	buf = alloca(file->length);

	err = vgsm_sim_file_read(file, buf, 0, file->length);
	if (err < 0) {
		ast_cli(fd, "Cannot read SIM file: %s\n", strerror(-err));
		goto out;
	}

	ast_cli(fd, "\nForbidden PLMNs:\n");
	for(i=0; i<file->length; i+=3) {

		if (buf[i] == 0xff &&
		    buf[i + 1] == 0xff &&
		    buf[i + 2] == 0xff)
			continue;

		__u16 mcc = (buf[i] & 0x0f) * 100 +
			    ((buf[i] & 0xf0) >> 4) * 10 +
			    (buf[i + 1] & 0x0f);

		__u16 mnc = (buf[i + 2] & 0x0f) * 10+
			    ((buf[i + 2] & 0xf0) >> 4);

		if ((buf[i + 1] & 0xf0) != 0xf0)
			    mnc += ((buf[i + 1] & 0xf0) >> 4) * 100;

		ast_cli(fd, "  %03d%02d", mcc, mnc);

		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(mcc, mnc);
		if (op_info)
			ast_cli(fd, " %s - %s",
				op_info->name,
				op_info->country ? op_info->country->name : "");

		ast_cli(fd, "\n");
	}

	vgsm_sim_file_put(file);

	/***************/

	file = vgsm_sim_file_alloc(sim);
	if (!file)
		goto out;

	vgsm_sim_file_open(file, 0x6F05);

	buf = alloca(file->length);
	err = vgsm_sim_file_read(file, buf, 0, file->length);
	if (err < 0) {
		ast_cli(fd, "Cannot read SIM file: %s\n", strerror(-err));
		goto out;
	}

	ast_cli(fd, "\nPreferred languages:\n");
	for(i=0; i<file->length; i++) {
		ast_cli(fd, "  %s\n", vgsm_sim_language_to_text(buf[i]));
	}

	vgsm_sim_file_put(file);

	ast_cli(fd, "\n");
#endif

out:

	ast_mutex_unlock(&me->lock);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_network(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	struct vgsm_me_config *mc = me->current_config;

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_OFFLINE) {
		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		goto out;
	}

	ast_cli(fd,
		"\nNetwork: \n"
		"  Operator Selection: %s\n",
		vgsm_me_operator_selection_to_text(
			mc->operator_selection));

	if (mc->operator_selection != VGSM_OPSEL_AUTOMATIC) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(
				mc->operator_mcc, mc->operator_mnc);

		if (op_info) {
			ast_cli(fd,
				"  Desidered network:"
				" %03d%02d (%s - %s - %s)\n",
				op_info->mcc,
				op_info->mnc,
				op_info->name,
				op_info->country ? op_info->country->name :
					"Unknown",
				op_info->bands);
		} else {
			ast_cli(fd,
				"  Desidered network: %03d%02d)\n",
				mc->operator_mcc,
				mc->operator_mnc);
		}
	}

	ast_cli(fd,
		"  Status: %s\n",
		vgsm_net_status_to_text(me->net.status));

	if (me->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    me->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING)
		goto out;

	struct vgsm_operator_info *op_info;
	op_info = vgsm_operators_search(me->net.mcc, me->net.mnc);

	if (op_info) {
		ast_cli(fd,
			"  Current network:"
			" %03u%02u (%s - %s - %s)\n",
			op_info->mcc,
			op_info->mnc,
			op_info->name,
			op_info->country ? op_info->country->name : "Unknown",
			op_info->bands);
	} else {
		ast_cli(fd,
			"  Current network: %03u%02u\n",
			me->net.mcc,
			me->net.mnc);
	}

	ast_cli(fd, "\nServing cell\n");
	ast_cli(fd,
		"  MCC MNC  LAC   ID"
		" BSIC ARFCN     RxLev\n");

	ast_cli(fd,
		"  %03d  %02d %04x %04x %4d %5d"
		" %5d dBm\n",
		me->net.sci.mcc,
		me->net.sci.mnc,
		me->net.sci.lac,
		me->net.sci.id,
		me->net.sci.bsic,
		me->net.sci.arfcn,
		-me->net.sci.rx_lev);

	ast_cli(fd,
		"  RxLev Sub: %d dBm\n"
		"  RxLev Full: %d dBm\n"
		"  RxQual: %d (BER %s)\n"
		"  RxQual Sub: %d (BER %s)\n"
		"  RxQual Full: %d (BER %s)\n"
		"  Timeslot: %d\n"
		"  TA: %d\n",
		-me->net.sci2.rx_lev_sub,
		-me->net.sci2.rx_lev_full,
		me->net.sci2.rx_qual,
		vgsm_qual_to_text(me->net.sci2.rx_qual),
		me->net.sci2.rx_qual_sub,
		vgsm_qual_to_text(me->net.sci2.rx_qual_sub),
		me->net.sci2.rx_qual_full,
		vgsm_qual_to_text(me->net.sci2.rx_qual_full),
		me->net.sci2.timeslot,
		me->net.sci2.ta);

	if (me->net.sci2.rssi == 0)
		ast_cli(fd, "  RSSI: <= -113 dBm\n");
	else if (me->net.sci2.rssi == 31)
		ast_cli(fd, "  RSSI: >= -51 dB,\n");
	else if (me->net.sci2.rssi == 99)
		ast_cli(fd, "  RSSI: N/A\n");
	else
		ast_cli(fd, "  RSSI: %d dBm\n",
			-113 + (me->net.sci2.rssi * 2));

	ast_cli(fd, "  BER: %d (%s)\n",
		me->net.sci2.ber,
		vgsm_qual_to_text(me->net.sci2.ber));

	if (me->net.ncells) {
		ast_cli(fd, "\nAdjacent cells (%d)\n",
			me->net.ncells);

		ast_cli(fd,
			"  #  MCC MNC  LAC   ID"
			" BSIC ARFCN     RxLev\n");

		int i;
		for (i=0; i<me->net.ncells; i++) {
			ast_cli(fd,
				" %2d: %03d  %02d %04x %04x %4d %5d"
				" %5d dBm\n",
				i + 1,
				me->net.nci[i].mcc,
				me->net.nci[i].mnc,
				me->net.nci[i].lac,
				me->net.nci[i].id,
				me->net.nci[i].bsic,
				me->net.nci[i].arfcn,
				-me->net.nci[i].rx_lev);
		}
	}

out:
	ast_cli(fd, "\n");

	ast_mutex_unlock(&me->lock);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_statistics(int fd, struct vgsm_me *me)
{
	struct vgsm_counter *counter;

	ast_mutex_lock(&me->lock);
	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_OFFLINE) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		return RESULT_FAILURE;
	}

	ast_cli(fd, "\nStatistics:\n");

	ast_cli(fd, "  Inbound: %d\n", me->stats.inbound);
	list_for_each_entry(counter, &me->stats.inbound_counters, node) {
		ast_cli(fd, "    %s/%s: %d\n",
			vgsm_cause_location_to_text(counter->location),
			vgsm_cause_reason_to_text(counter->location,
				counter->reason),
			counter->count);
	}

	ast_cli(fd, "  Outbound: %d\n", me->stats.outbound);
	list_for_each_entry(counter, &me->stats.outbound_counters, node) {
		ast_cli(fd, "    %s/%s: %d\n",
			vgsm_cause_location_to_text(counter->location),
			vgsm_cause_reason_to_text(counter->location,
				counter->reason),
			counter->count);
	}

	ast_cli(fd, "\n");

	ast_mutex_unlock(&me->lock);

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 180 * SEC, "AT^STCD");
	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);

		vgsm_req_put(req);

		return RESULT_FAILURE;
	}

	ast_cli(fd, "Total call duration: %s\n",
		vgsm_req_first_line(req)->text + strlen("^STCD: "));

	vgsm_req_put(req);

	req = vgsm_req_make_wait(&me->comm, 180 * SEC, "AT^SLCD");
	res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);

		vgsm_req_put(req);

		return RESULT_FAILURE;
	}

	ast_cli(fd, "Last call duration: %s\n",
		vgsm_req_first_line(req)->text + strlen("^SLCD: "));

	vgsm_req_put(req);

	ast_cli(fd, "\n");

	return RESULT_SUCCESS;
}

static int vgsm_me_show_calls(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	ast_cli(fd, "\nCalls:\n");
	ast_cli(fd, "  #  State      Dir T Bearer          Channel\n");

	int i;
	for(i=0; i<ARRAY_SIZE(me->calls); i++) {
		if (me->calls[i].state == VGSM_CALL_STATE_UNUSED)
			continue;

		ast_cli(fd, "  %1d: %-10s %-3s %c %-15s %s\n",
			i + 1,
			vgsm_call_state_to_text(me->calls[i].state),
			(me->calls[i].direction ==
				VGSM_CALL_DIRECTION_MOBILE_TERMINATED ?
					"IN" : "OUT"),
			me->calls[i].channel_assigned ? '*' : ' ',
			vgsm_call_bearer_to_text(me->calls[i].bearer),
			(i == 0 && me->vgsm_chan) ?
				me->vgsm_chan->ast_chan->name : "");
	}

	ast_cli(fd, "\n");

	ast_mutex_unlock(&me->lock);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_moni(int fd, struct vgsm_me *me)
{
	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, "AT^MONI");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put(req);
		return RESULT_FAILURE;
	}

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node)
		ast_cli(fd, "%s\n", line->text);

	vgsm_req_put(req);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_smong(int fd, struct vgsm_me *me)
{
	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, "AT^SMONG");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put(req);
		return RESULT_FAILURE;
	}

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node)
		ast_cli(fd, "%s\n", line->text);

	vgsm_req_put(req);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_serial(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);
	struct vgsm_me_config *mc;
	mc = vgsm_me_config_get(me->current_config);
	ast_mutex_unlock(&me->lock);

	vgsm_comm_cli_show_state(fd, &me->comm);

	struct serial_icounter_struct icount;
	if (ioctl(me->me_fd, TIOCGICOUNT, &icount) < 0) {
		ast_cli(fd, "ioctl(TIOCGICOUNT)\n");
		return RESULT_FAILURE;
	}

	int status;
	if (ioctl(me->me_fd, TIOCMGET, &status) < 0) {
		ast_cli(fd, "ioctl(TIOCMGET)\n");
		return RESULT_FAILURE;
	}

	ast_cli(fd,
		"  Flow conf.:  %s\n"
		"  Flow acutal: %s\n"
		"  RTS:         %s\n"
		"  CTS:         %s (%d)\n"
		"  DTR:         %s\n"
		"  DSR:         %s (%d)\n"
		"  RI:          %s (%d)\n"
		"  CD:          %s (%d)\n"
		"  RX:          %d\n"
		"  TX:          %d\n"
		"  Frame:       %d\n"
		"  Overrun:     %d\n"
		"  Parity:      %d\n"
		"  Break:       %d\n"
		"  Buf overrun: %d\n",
		vgsm_flow_control_to_text(mc->flow_control),
		vgsm_flow_control_to_text(me->flow_control),
		(status & TIOCM_RTS) ? "ON" : "OFF",
		(status & TIOCM_CTS) ? "ON" : "OFF", icount.cts,
		(status & TIOCM_DTR) ? "ON" : "OFF",
		(status & TIOCM_DSR) ? "ON" : "OFF", icount.dsr, 
		(status & TIOCM_RI)  ? "ON" : "OFF", icount.rng, 
		(status & TIOCM_CD)  ? "ON" : "OFF", icount.dcd,
		icount.rx,
		icount.tx,
		icount.frame, icount.overrun, icount.parity, icount.brk,
		icount.buf_overrun);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_summary(int fd, struct vgsm_me *me)
{
	ast_mutex_lock(&me->lock);

	ast_cli(fd, "%-10s: %s",
		me->name,
		vgsm_me_status_to_text(me->status));

	if (me->status == VGSM_ME_STATUS_READY ||
	    me->status == VGSM_ME_STATUS_OFFLINE) {
		ast_cli(fd, " %-17s",
			vgsm_net_status_to_text(me->net.status));

		if (me->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
		    me->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {

			struct vgsm_operator_info *op_info;
			op_info = vgsm_operators_search(me->net.mcc,
							me->net.mnc);

			if (op_info)
				ast_cli(fd, " \"%s\"", op_info->name_short);
			else
				ast_cli(fd, " %03u%02u", me->net.mcc,
							me->net.mnc);
		}

		if (me->vgsm_chan)
			ast_cli(fd, " - CALL[%s]",
				me->vgsm_chan->ast_chan->name);

		if (me->sending_sms)
			ast_cli(fd, " - SENDING_SMS");

	} else {
		if (me->status_reason)
			ast_cli(fd, "  %s", me->status_reason);
	}

	ast_cli(fd, "\n");

	ast_mutex_unlock(&me->lock);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_details(int fd, struct vgsm_me *me)
{
	vgsm_me_show_me(fd, me);
	vgsm_me_show_serial(fd, me);
	vgsm_me_show_forwarding(fd, me);
	vgsm_me_show_callwaiting(fd, me);
	vgsm_me_show_sim(fd, me);
	vgsm_me_show_network(fd, me);
	vgsm_me_show_statistics(fd, me);
	vgsm_me_show_calls(fd, me);
	vgsm_me_show_moni(fd, me);
	vgsm_me_show_smong(fd, me);

	return RESULT_SUCCESS;
}

static int vgsm_me_show_cli(int fd, int argc, char *argv[])
{
	int err;
	struct vgsm_me *me;

	if (argc >= 4) {
		me = vgsm_me_get_by_name(argv[3]);
		if (!me) {
			ast_cli(fd, "ME %s not found\n", argv[3]);
			err = RESULT_FAILURE;
			goto err_me_not_found;
		}

		if (argc >= 5) {
			if (!strcasecmp(argv[4], "forwarding"))
				err = vgsm_me_show_forwarding(fd, me);
			else if (!strcasecmp(argv[4], "callwaiting"))
				err = vgsm_me_show_callwaiting(fd, me);
			else if (!strcasecmp(argv[4], "sim"))
				err = vgsm_me_show_sim(fd, me);
			else if (!strcasecmp(argv[4], "network"))
				err = vgsm_me_show_network(fd, me);
			else if (!strcasecmp(argv[4], "statistics"))
				err = vgsm_me_show_statistics(fd, me);
			else if (!strcasecmp(argv[4], "calls"))
				err = vgsm_me_show_calls(fd, me);
			else if (!strcasecmp(argv[4], "moni"))
				err = vgsm_me_show_moni(fd, me);
			else if (!strcasecmp(argv[4], "smong"))
				err = vgsm_me_show_smong(fd, me);
			else if (!strcasecmp(argv[4], "serial"))
				err = vgsm_me_show_serial(fd, me);
			else if (!strcasecmp(argv[4], "details"))
				err = vgsm_me_show_details(fd, me);
			else {
				ast_cli(fd, "Command '%s' unrecognized\n",
					argv[4]);

				err = RESULT_SHOWUSAGE;
				goto err_command_unrecognized;
			}
		} else
			vgsm_me_show_me(fd, me);

		vgsm_me_put(me);

	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		list_for_each_entry(me, &vgsm.mes_list, node)
			vgsm_me_show_summary(fd, me);
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;

err_command_unrecognized:
	vgsm_me_put(me);
err_me_not_found:

	return err;
}

static char *vgsm_me_show_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "forwarding", "callwaiting", "sim", "network",
				"statistics", "calls", "moni", "smong",
				"serial", "details" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);
	case 4:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	}

	return NULL;
}

static char vgsm_me_show_help[] =
"Usage: show vgsm mes [<me> [<category>]]\n"
"\n"
"	Display informations on vGSM MEs\n"
"\n"
"	<category> may be one of the following:\n"
"\n"
"		forwarding	show current status of call forwarding\n"
"				supplementary service\n"
"\n"
"		callwaiting	show current status of call waiting\n"
"				supplementary service\n"
"\n"
"		sim		show informations related to the SIM card\n"
"				inserted in <me>\n"
"\n"
"		network		show informations regarding the GSM network\n"
"				received by the me\n"
"\n"
"		statistics	show call statistics: inbound/outbound\n"
"				counters, release codes and duration\n"
"\n"
"		calls		show active calls on selected me\n"
"\n"
"		details		show detailed informations about me\n";

static struct ast_cli_entry vgsm_me_show =
{
	{ "vgsm", "me", "show", NULL },
	vgsm_me_show_cli,
	"Displays vGSM ME informations",
	vgsm_me_show_help,
	vgsm_me_show_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_power_on(int fd, struct vgsm_me *me)
{
	if (me->status == VGSM_ME_STATUS_UNCONFIGURED ||
	    me->status == VGSM_ME_STATUS_CLOSED ||
	    me->status == VGSM_ME_STATUS_FAILED) {
		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		return RESULT_FAILURE;
	} else if (me->status == VGSM_ME_STATUS_POWERING_ON) {
		ast_cli(fd, "ME '%s' is already powering off\n", me->name);
		return RESULT_FAILURE;
	} else if (me->status != VGSM_ME_STATUS_OFF) {
		ast_cli(fd, "ME '%s' is already powered on\n", me->name);
		return RESULT_FAILURE;
	}

	vgsm_me_ignite(me);

	vgsm_me_set_status(me,
		VGSM_ME_STATUS_POWERING_ON,
		POWERING_ON_TIMEOUT,
		"CLI request");

	return RESULT_SUCCESS;
}

static int vgsm_me_power_off(int fd, struct vgsm_me *me)
{
	struct vgsm_comm *comm = &me->comm;

	if (me->status == VGSM_ME_STATUS_UNCONFIGURED ||
	    me->status == VGSM_ME_STATUS_CLOSED ||
	    me->status == VGSM_ME_STATUS_FAILED) {
		ast_cli(fd, "ME '%s' is not available\n", me->name);
		return RESULT_FAILURE;
	} else if (me->status == VGSM_ME_STATUS_OFF) {
		ast_cli(fd, "ME is already powered off\n");
		return RESULT_FAILURE;
	} else if (me->status == VGSM_ME_STATUS_POWERING_OFF) {
		ast_cli(fd, "ME is already powering off\n");
		return RESULT_FAILURE;
	}

	vgsm_me_set_status(me,
			VGSM_ME_STATUS_POWERING_OFF,
			POWERING_OFF_TIMEOUT,
			"CLI request");

	int res = vgsm_req_make_wait_result(comm, 3 * SEC, "AT^SMSO");
	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_power_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing command\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_command;
	}
	const char *command = argv[4];

	int (*func)(int fd, struct vgsm_me *me);

	if (!strcasecmp(command, "on"))
		func = vgsm_me_power_on;
	else if (!strcasecmp(command, "off"))
		func = vgsm_me_power_off;
	else {
		ast_cli(fd, "Unknown command '%s'\n", command);
		err = RESULT_SHOWUSAGE;
		goto err_unknown_command;
	}

	if (strcmp(me_name, "*")) {
		struct vgsm_me *me;
		me = vgsm_me_get_by_name(me_name);
		if (!me) {
			ast_cli(fd, "Cannot find me '%s'\n", me_name);
			err = RESULT_FAILURE;
			goto err_me_not_found;
		}

		err = func(fd, me);
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node) {
			ast_mutex_lock(&me->lock);
			func(fd, me);
			ast_mutex_unlock(&me->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}


	return RESULT_SUCCESS;

err_me_not_found:
err_unknown_command:
err_missing_command:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_power_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "on", "off" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);

	case 4:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_me_cli_power_help[] =
"Usage: vgsm me power <me> <on|off>\n"
"\n"
"	<me>	ME name or '*' for every ME.\n"
"\n"
"	on\n"
"		Power on or off the specified me.\n"
"\n"
"	off\n"
"		Power-off will be graceful (requesting de-registration from\n"
"		the network. If, however, the me is not responding,\n"
"		the me will be forcibly shut down.\n";

static struct ast_cli_entry vgsm_me_cli_power =
{
	{ "vgsm", "me", "power", NULL },
	vgsm_me_cli_power_func,
	"Power control",
	vgsm_me_cli_power_help,
	vgsm_me_cli_power_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_reset_do(int fd, struct vgsm_me *me)
{
	struct vgsm_comm *comm = &me->comm;

	if (me->status != VGSM_ME_STATUS_READY &&
	    me->status != VGSM_ME_STATUS_WAITING_SIM &&
	    me->status != VGSM_ME_STATUS_WAITING_PIN) {
		ast_cli(fd, "ME '%s' is not available\n", me->name);
		return RESULT_FAILURE;
	}

	vgsm_me_set_status(me,
			VGSM_ME_STATUS_RESETTING,
			RESET_TIMEOUT,
			"CLI request");

	int res = vgsm_req_make_wait_result(comm, 3 * SEC, "AT+CFUN=1,1");
	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_reset_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	if (strcmp(me_name, "*")) {
		struct vgsm_me *me;
		me = vgsm_me_get_by_name(me_name);
		if (!me) {
			ast_cli(fd, "Cannot find me '%s'\n", me_name);
			err = RESULT_FAILURE;
			goto err_me_not_found;
		}

		ast_mutex_lock(&me->lock);
		err = vgsm_me_cli_reset_do(fd, me);
		ast_mutex_unlock(&me->lock);
	} else {
		err = RESULT_SUCCESS;

		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node) {
			ast_mutex_lock(&me->lock);
			err = vgsm_me_cli_reset_do(fd, me);
			ast_mutex_unlock(&me->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	if (err != RESULT_SUCCESS)
		goto err_reset_do;

	return RESULT_SUCCESS;

err_reset_do:
err_me_not_found:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_reset_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_me_cli_reset_help[] =
"Usage: vgsm me reset <me>\n"
"\n"
"	Initiate ME software reset\n";

static struct ast_cli_entry vgsm_me_cli_reset =
{
	{ "vgsm", "me", "reset", NULL },
	vgsm_me_cli_reset_func,
	"Put the ME online or offline",
	vgsm_me_cli_reset_help,
	vgsm_me_cli_reset_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_service_on(int fd, struct vgsm_me *me)
{
	me->in_service = TRUE;

	if (me->status == VGSM_ME_STATUS_OFFLINE) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_READY,
				READY_UPDATE_TIME,
				" ");
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_service_off(int fd, struct vgsm_me *me)
{
	if (me->status == VGSM_ME_STATUS_OFFLINE) {
	} else if (me->status == VGSM_ME_STATUS_READY) {
		vgsm_me_set_status(me,
				VGSM_ME_STATUS_OFFLINE,
				-1,
				"CLI request");
	} else {
		me->in_service = FALSE;
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_service_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing command\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_command;
	}
	const char *command = argv[4];

	int (*func)(int fd, struct vgsm_me *me);

	if (!strcasecmp(command, "on"))
		func = vgsm_me_cli_service_on;
	else if (!strcasecmp(command, "off"))
		func = vgsm_me_cli_service_off;
	else {
		ast_cli(fd, "Unknown command '%s'\n", command);
		err = RESULT_SHOWUSAGE;
		goto err_unknown_command;
	}

	if (strcmp(me_name, "*")) {
		struct vgsm_me *me;
		me = vgsm_me_get_by_name(me_name);
		if (!me) {
			ast_cli(fd, "Cannot find me '%s'\n", me_name);
			err = RESULT_FAILURE;
			goto err_me_not_found;
		}

		ast_mutex_lock(&me->lock);
		err = func(fd, me);
		ast_mutex_unlock(&me->lock);
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node) {
			ast_mutex_lock(&me->lock);
			func(fd, me);
			ast_mutex_unlock(&me->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}


	return RESULT_SUCCESS;

err_me_not_found:
err_unknown_command:
err_missing_command:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_service_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "on", "off" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);

	case 4:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_me_cli_service_help[] =
"Usage: vgsm me service <on|off> <me>\n"
"\n"
"	<me>\n"
"		ME name or '*' for every ME.\n"
"\n"
"	on\n"
"		Put the specified me in-service.\n"
"\n"
"	off\n"
"		Put the specified ME offline.\n";

static struct ast_cli_entry vgsm_me_cli_service =
{
	{ "vgsm", "me", "service", NULL },
	vgsm_me_cli_service_func,
	"Put the ME online or offline",
	vgsm_me_cli_service_help,
	vgsm_me_cli_service_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_identify_do(int fd, struct vgsm_me *me, int value)
{
	if (me->status == VGSM_ME_STATUS_UNCONFIGURED ||
	    me->status == VGSM_ME_STATUS_CLOSED ||
	    me->status == VGSM_ME_STATUS_FAILED) {
		ast_cli(fd, "ME '%s' is not available\n", me->name);
		return RESULT_FAILURE;
	}

	if (ioctl(me->me_fd, VGSM_IOC_IDENTIFY, value) < 0) {
		ast_cli(fd, "ioctl(VGSM_IOC_IDENTIFY): %s\n", strerror(errno));
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_identify_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	int value = 1;
	if (argc > 4) {
		const char *command = argv[4];

		if (!strcasecmp(command, "off"))
			value = 0;
		else {
			ast_cli(fd, "Unknown command '%s'\n", command);
			err = RESULT_SHOWUSAGE;
			goto err_unknown_command;
		}
	}

	if (strcmp(me_name, "*")) {
		struct vgsm_me *me;
		me = vgsm_me_get_by_name(me_name);
		if (!me) {
			ast_cli(fd, "Cannot find me '%s'\n", me_name);
			err = RESULT_FAILURE;
			goto err_me_not_found;
		}

		ast_mutex_lock(&me->lock);
		err = vgsm_me_cli_identify_do(fd, me, value);
		ast_mutex_unlock(&me->lock);
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node) {
			ast_mutex_lock(&me->lock);
			err = vgsm_me_cli_identify_do(fd, me, value);
			ast_mutex_unlock(&me->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;

err_me_not_found:
err_unknown_command:
err_missing_me:

	return err;
}
static char *vgsm_me_cli_identify_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "off" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);

	case 4:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_me_cli_identify_help[] =
"Usage: vgsm me identify <me> [off]\n"
"\n"
"	<me>\n"
"		ME name or * for every ME.\n"
"\n"
"	off\n"
"		Disables frontal LED flashing to identify the\n"
"		antenna connector associated with the specified me.\n";

static struct ast_cli_entry vgsm_me_cli_identify =
{
	{ "vgsm", "me", "identify", NULL },
	vgsm_me_cli_identify_func,
	"Set operation selection method",
	vgsm_me_cli_identify_help,
	vgsm_me_cli_identify_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_operator_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing mode parameter\n\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_mode;
	}
	const char *mode = argv[4];

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find me '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	struct vgsm_comm *comm = &me->comm;

	if (!strcasecmp(mode, "auto")) {
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=0,2");
	} else if (!strcasecmp(mode, "none")) {
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=2,2");
	} else {
		int lai = atoi(mode);
		int mode;

		if (argc > 5 && !strcasecmp(argv[5], "fallback"))
			mode = 4;
		else
			mode = 1;

		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=%d,2,%05u",
			mode, lai);
	}

	if (err != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(err),
			err);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;

err_me_not_found:
err_missing_mode:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_operator_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *modes[] = { "auto", "none", "<LAI>" };
	char *options[] = { "fallback" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);

	case 4:
		for(i=state; i<ARRAY_SIZE(modes); i++) {
			if (!strncasecmp(word, modes[i], strlen(word)))
				return strdup(modes[i]);
		}
	break;

	case 5:
		for(i=state; i<ARRAY_SIZE(options); i++) {
			if (!strncasecmp(word, options[i], strlen(word)))
				return strdup(options[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_me_cli_operator_help[] =
"Usage: vgsm me operator <me> <auto | none | LAI> [fallback]\n"
"\n"
"	Changes the operator selection mode.\n"
"\n"
"	<me>	ME name or * for every ME.\n"
"\n"
"	auto: Automatically select the best operator\n"
"	none: Deregister and disable further registration attempts\n"
"	LAI: Manually select the operator specified by LAI (MCC+MNC).\n"
"\n"
"	If 'fallback' is specified, fall back to automatic if\n"
"	the manually selected operator is not available.\n";

static struct ast_cli_entry vgsm_me_cli_operator =
{
	{ "vgsm", "me", "operator", NULL },
	vgsm_me_cli_operator_func,
	"Set operation selection method",
	vgsm_me_cli_operator_help,
	vgsm_me_cli_operator_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_pin_set_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 5) {
		ast_cli(fd, "Missing ME name\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[4];

	if (argc < 6) {
		ast_cli(fd, "Missing OLDPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_oldpin;
	}
	const char *oldpin = argv[5];

	if (!vgsm_pin_valid(oldpin)) {
		ast_cli(fd, "OLDPIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_oldpin_invalid;
	}

	if (argc < 7) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_newpin;
	}
	const char *newpin = argv[6];

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find ME '%s'\n", me_name);
		err = RESULT_SHOWUSAGE;
		goto err_me_not_found;
	}

	int res;
	if (!strcasecmp(newpin, "enabled")) {
		res = vgsm_req_make_wait_result(&me->comm, 180 * SEC,
			"AT+CLCK=SC,1,\"%s\"", oldpin);
	} else if (!strcasecmp(newpin, "disabled")) {
		res = vgsm_req_make_wait_result(&me->comm, 180 * SEC,
			"AT+CLCK=SC,0,\"%s\"", oldpin);
	} else {
		if (!vgsm_pin_valid(newpin)) {
			ast_cli(fd, "NEWPIN contains invalid characters\n");
			err = RESULT_FAILURE;
			goto err_newpin_invalid;
		}

		res = vgsm_req_make_wait_result(&me->comm, 180 * SEC,
			"AT+CPWD=SC,\"%s\",\"%s\"",
			oldpin, newpin);
	}

	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Unable to complete command: %s (%d)\n",
			vgsm_me_error_to_text(res), res);
		err = RESULT_FAILURE;
		goto err_response;
	}

	vgsm_me_put(me);

	return RESULT_SUCCESS;

err_response:
err_newpin_invalid:
	vgsm_me_put(me);
err_me_not_found:
err_missing_newpin:
err_oldpin_invalid:
err_missing_oldpin:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_pin_set_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "enabled", "disabled" };
	int i;

	switch(pos) {
	case 4:
		return vgsm_me_completion(line, word, state);

	case 6:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_me_cli_pin_set_help[] =
"Usage: vgsm pin set <me> <OLDPIN> <NEWPIN|enabled|disabled>\n"
"\n"
"	Set, enable or disable the PIN on the SIM installed in me\n"
"	<me>.\n"
"\n"
"	<me>		ME name\n"
"	<OLDPIN>	Current PIN\n"
"	<NEWPIN>	New PIN\n"
"	enabled		Enable PIN check on the SIM card\n"
"	disabled	Disable PIN check on the SIM card\n";

static struct ast_cli_entry vgsm_me_cli_pin_set =
{
	{ "vgsm", "me", "pin", "set", NULL },
	vgsm_me_cli_pin_set_func,
	"Set, enable or disable PIN on the selected ME",
	vgsm_me_cli_pin_set_help,
	vgsm_me_cli_pin_set_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_pin_input_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 5) {
		ast_cli(fd, "Missing ME name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_me_name;
	}
	const char *me_name = argv[4];

	if (argc < 6) {
		ast_cli(fd, "Missing PIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_pin;
	}
	const char *pin = argv[5];

	if (!vgsm_pin_valid(pin)) {
		ast_cli(fd, "PIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_pin_invalid;
	}

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find ME '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	struct vgsm_comm *comm = &me->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	if (req->err != VGSM_RESP_OK) {
		vgsm_me_failed(me, req->err);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	const struct vgsm_req_line *first_line;
	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		int res = vgsm_req_make_wait_result(comm, 180 * SEC,
				"AT+CPIN=\"%s\"", pin);
		if (res != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_me_error_to_text(res), res);
			err = RESULT_FAILURE;
			goto err_send_pin;
		}

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_WAITING_INITIALIZATION,
			0, "PIN entered");

	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2\n");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		ast_cli(fd, "SIM requires PUK\n");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2\n");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else {
		ast_cli(fd, "Unknown response '%s'\n", first_line->text);
		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put(req);
	vgsm_me_put(me);

	return RESULT_SUCCESS;

err_unknown_response:
err_send_pin:
err_not_waiting_pin:
	vgsm_req_put(req);
err_req_make:
	vgsm_me_put(me);
err_me_not_found:
err_pin_invalid:
err_no_pin:
err_no_me_name:

	return err;
}

static char *vgsm_me_cli_pin_input_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 4:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_me_cli_pin_input_help[] =
"Usage: vgsm me pin input <me> <PIN>\n"
"\n"
"	Manually input PIN to selected ME\n";

static struct ast_cli_entry vgsm_me_cli_pin_input =
{
	{ "vgsm", "me", "pin", "input", NULL },
	vgsm_me_cli_pin_input_func,
	"Manually input PIN to selected ME",
	vgsm_me_cli_pin_input_help,
	vgsm_me_cli_pin_input_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_puk_input_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_me_name;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing PUK\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_puk;
	}
	const char *puk = argv[4];

	if (!vgsm_pin_valid(puk)) {
		ast_cli(fd, "PUK contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_puk_invalid;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_newpin;
	}
	const char *newpin = argv[5];

	if (!vgsm_pin_valid(newpin)) {
		ast_cli(fd, "NEWPIN contains invalid characters\n");
		err = RESULT_FAILURE;
		goto err_newpin_invalid;
	}

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find ME '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	struct vgsm_comm *comm = &me->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_me_failed(me, err);
		vgsm_req_put(req);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	const struct vgsm_req_line *first_line;
	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {
		ast_cli(fd, "SIM requires PIN\n");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2\n");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {

		int err = vgsm_req_make_wait_result(comm, 180 * SEC,
				"AT+CPIN=\"%s\",\"%s\"", puk, newpin);
		if (err != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_me_error_to_text(err), err);
			err = RESULT_FAILURE;
		goto err_invalid_state;
		}

		vgsm_me_set_status(me,
			VGSM_ME_STATUS_WAITING_INITIALIZATION, 0,
			"PUK entered");

	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2\n");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else {
		ast_cli(fd, "Unknown response '%s'\n", first_line->text);

		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put(req);
	vgsm_me_put(me);

	return RESULT_SUCCESS;

err_unknown_response:
err_invalid_state:
	vgsm_req_put(req);
err_req_make:
	vgsm_me_put(me);
err_me_not_found:
err_newpin_invalid:
err_no_newpin:
err_puk_invalid:
err_no_puk:
err_no_me_name:

	return err;
}

static char *vgsm_me_cli_puk_input_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_me_cli_puk_input_help[] =
"Usage: vgsm me puk input <me> <PUK>\n"
"\n"
"	Manually input PUK to selected me\n"
"\n"
"	WARNING: Inputing the wrong PUK for 10 times will render the SIM card\n"
"	         useless, you will need to have it replaced from your\n"
"	         operator.\n";

static struct ast_cli_entry vgsm_me_cli_puk_input =
{
	{ "vgsm", "me", "puk", "input", NULL },
	vgsm_me_cli_puk_input_func,
	"Manually input PUK to selected ME",
	vgsm_me_cli_puk_input_help,
	vgsm_me_cli_puk_input_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_sms_send_func(int fd, int argc, char *argv[])
{
	int err = RESULT_SUCCESS;

	if (argc < 5) {
		ast_cli(fd, "Missing me\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[4];

	if (argc < 6) {
		ast_cli(fd, "Missing phone number\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_number;
	}
	const char *phone_number = argv[5];

	if (argc < 7) {
		ast_cli(fd, "Missing text\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_text;
	}
	const char *text = argv[6];

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find ME '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME '%s' is not ready\n", me->name);
		err = RESULT_FAILURE;
		goto err_me_not_ready;
	}

	if (me->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    me->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME %s not registered\n", me->name);
		err = RESULT_FAILURE;
		goto err_me_not_registered;
	}

	if (me->sending_sms) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "ME '%s' is already sending a SMS\n", me->name);
		err = RESULT_FAILURE;
		goto err_already_sending_sms;
	}

	me->sending_sms = TRUE;

	struct vgsm_sms_submit *sms;
	sms = vgsm_sms_submit_alloc();
	if (!sms) {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "Cannot allocate SMS\n");
		err = RESULT_FAILURE;
		goto err_sms_alloc;
	}

	sms->me = vgsm_me_get(me);

	if (strlen(me->current_config->smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address,
				&me->current_config->smcc_address);
	} else if (strlen(me->sim.smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address, &me->sim.smcc_address);
	} else {
		ast_mutex_unlock(&me->lock);

		ast_cli(fd, "Services Center number not set\n");
		err = RESULT_FAILURE;
		goto err_no_smcc;
	}

	ast_mutex_unlock(&me->lock);

	if (vgsm_number_parse(&sms->dest, phone_number) < 0) {
		ast_cli(fd, "Number '%s' is invalid\n", phone_number);
		err = RESULT_FAILURE;
		goto err_invalid_number;
	}

	if (argc >= 8)
		sms->message_class = atoi(argv[7]);
	else
		sms->message_class = 1;

	size_t slen;
	slen = mbstowcs(NULL, text, 0);
	if(slen == -1)
		goto err_invalid_mbstring;

	sms->text = malloc((slen + 1) * sizeof(wchar_t));
	if(!sms->text)
		goto err_malloc_sms_text;

	mbstowcs(sms->text, text, slen);
	sms->text[slen] = L'\0';

	err = vgsm_sms_submit_prepare(sms);
	if (err == -ENOSPC) {
		ast_cli(fd, "Message too big\n");
		err = RESULT_FAILURE;
		goto err_submit_prepare;
	} else if (err < 0) {
		ast_cli(fd, "Invalid message content\n");
		err = RESULT_FAILURE;
		goto err_submit_prepare;
	}

	struct vgsm_req *req = vgsm_req_make_sms(
		&me->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);

	vgsm_req_wait(req);

	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		vgsm_req_put(req);
		ast_cli(fd,
			"Error sending SMS: %s (%d)\n",
			vgsm_me_error_to_text(res),
			res);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	vgsm_req_put(req);

	ast_mutex_lock(&me->lock);
	me->sending_sms = FALSE;
	ast_mutex_unlock(&me->lock);

	return RESULT_SUCCESS;

err_req_make:
err_submit_prepare:
err_invalid_mbstring:
err_malloc_sms_text:
err_invalid_number:
err_no_smcc:
	vgsm_sms_submit_put(sms);
err_sms_alloc:
	ast_mutex_lock(&me->lock);
	me->sending_sms = FALSE;
	ast_mutex_unlock(&me->lock);
err_already_sending_sms:
err_me_not_registered:
err_me_not_ready:
	vgsm_me_put(me);
err_me_not_found:
err_missing_text:
err_missing_number:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_sms_send_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 4:
		return vgsm_me_completion(line, word,state);
	}

	return NULL;
}

static char vgsm_me_cli_sms_send_help[] =
"Usage: vgsm me sms send <me> <number> <text> [class]\n"
"\n"
"	Send short message to <number> using me <me>.\n"
"\n"
"	<text> is the text to send, in 7-bit ASCII format.\n"
"	This is meant to be just a testing command, other charsets beside\n"
"	ASCII are not supported, neither are various other SMS parameters;\n"
"\n"
"	The full SMS interface is implemented throught the manager\n"
"	interface.\n";

static struct ast_cli_entry vgsm_me_cli_sms_send =
{
	{ "vgsm", "me", "sms", "send", NULL },
	vgsm_me_cli_sms_send_func,
	"Send a SMS message",
	vgsm_me_cli_sms_send_help,
	vgsm_me_cli_sms_send_complete,
};

/*---------------------------------------------------------------------------*/
static int vgsm_me_cli_forwarding_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_me_name;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing command\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_command;
	}
	const char *command = argv[4];

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find ME '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	if (!strcasecmp(command, "off")) {
//		err = do_vgsm_me_cli_forwarding_off(fd, me);
//		if (err != RESULT_SUCCESS)
//			goto err_forwarding_off;
	} else {
		ast_cli(fd, "Unknown command '%s'\n", command);
		err = RESULT_SHOWUSAGE;
		goto err_unknown_command;
	}

	vgsm_me_put(me);

	return RESULT_SUCCESS;

err_unknown_command:
	vgsm_me_put(me);
err_me_not_found:
err_no_command:
err_no_me_name:

	return err;
}

static char *vgsm_me_cli_forwarding_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "off" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);
	case 4:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	}

	return NULL;
}

static char vgsm_me_cli_forwarding_help[] =
"Usage: vgsm me forwarding <me> <set|>\n"
"\n"
"	Set call forwarding for the specified me (not yet implemented)\n";

static struct ast_cli_entry vgsm_me_cli_forwarding =
{
	{ "vgsm", "me", "forwarding", NULL },
	vgsm_me_cli_forwarding_func,
	"Set call forwarding for specified me",
	vgsm_me_cli_forwarding_help,
	vgsm_me_cli_forwarding_complete
};

/*---------------------------------------------------------------------------*/

static int vgsm_me_cli_rawcommand_func(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing ME name\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_me;
	}
	const char *me_name = argv[3];

	if (argc < 5) {
		ast_cli(fd, "Missing command\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_command;
	}
	const char *command = argv[4];

	struct vgsm_me *me;
	me = vgsm_me_get_by_name(me_name);
	if (!me) {
		ast_cli(fd, "Cannot find me '%s'\n", me_name);
		err = RESULT_FAILURE;
		goto err_me_not_found;
	}

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&me->comm, 5 * SEC, command);
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_me_error_to_text(vgsm_req_status(req)),
			vgsm_req_status(req));
		vgsm_req_put(req);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node)
		ast_cli(fd, "%s\n", line->text);

	vgsm_req_put(req);

	return RESULT_SUCCESS;

err_req_make:
err_me_not_found:
err_missing_command:
err_missing_me:

	return err;
}

static char *vgsm_me_cli_rawcommand_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_me_cli_rawcommand_help[] =
"Usage: vgsm me rawcommand <me> <command>\n"
"\n"
"	Send a raw AT command. Only for debugging purposes!\n";

static struct ast_cli_entry vgsm_me_cli_rawcommand =
{
	{ "vgsm", "me", "rawcommand", NULL },
	vgsm_me_cli_rawcommand_func,
	"Send AT raw command",
	vgsm_me_cli_rawcommand_help,
	vgsm_me_cli_rawcommand_complete
};

/*---------------------------------------------------------------------------*/

#ifdef DEBUG_CODE
static int vgsm_me_cli_debug_state(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_state = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_state = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_call(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_call = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_call = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_atcommands(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->comm.debug_messages = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->comm.debug_messages = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_serial(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->comm.debug_characters = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->comm.debug_characters = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_sms(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_sms = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_sms = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_cbm(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_cbm = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_cbm = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_jitbuf(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_jitbuf = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_jitbuf = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_frames(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->debug_frames = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->debug_frames = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_sim(
	int fd, struct vgsm_me *me, BOOL enable)
{
	if (me) {
		me->mesim.debug = enable;
	} else {
		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		struct vgsm_me *me;
		list_for_each_entry(me, &vgsm.mes_list, node)
			me->mesim.debug = enable;
		ast_rwlock_unlock(&vgsm.mes_list_lock);
	}

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_all(int fd, BOOL enable)
{
	ast_rwlock_rdlock(&vgsm.mes_list_lock);
	struct vgsm_me *me;
	list_for_each_entry(me, &vgsm.mes_list, node) {
		me->comm.debug_messages = enable;
		me->mesim.debug = enable;
		me->debug_sms = enable;
		me->debug_cbm = enable;
		me->debug_jitbuf = enable;
	}
	ast_rwlock_unlock(&vgsm.mes_list_lock);

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_do(int fd, int argc, char *argv[],
				int args, BOOL enable)
{
	int err = 0;

	if (argc < args)
		return RESULT_SHOWUSAGE;

	struct vgsm_me *me = NULL;

	if (argc < args + 1) {
		err = vgsm_me_cli_debug_all(fd, enable);
	} else {
		if (argc > args + 1) {
			me = vgsm_me_get_by_name(argv[args + 1]);
			if (!me) {
				ast_cli(fd, "Cannot find me '%s'\n",
					argv[args]);
				return RESULT_FAILURE;
			}
		}

		if (!strcasecmp(argv[args], "state"))
			err = vgsm_me_cli_debug_state(fd, me, enable);
		else if (!strcasecmp(argv[args], "call"))
			err = vgsm_me_cli_debug_call(fd, me, enable);
		else if (!strcasecmp(argv[args], "atcommands"))
			err = vgsm_me_cli_debug_atcommands(fd, me, enable);
		else if (!strcasecmp(argv[args], "serial"))
			err = vgsm_me_cli_debug_serial(fd, me, enable);
		else if (!strcasecmp(argv[args], "sms"))
			err = vgsm_me_cli_debug_sms(fd, me, enable);
		else if (!strcasecmp(argv[args], "cbm"))
			err = vgsm_me_cli_debug_cbm(fd, me, enable);
		else if (!strcasecmp(argv[args], "jitbuf"))
			err = vgsm_me_cli_debug_jitbuf(fd, me, enable);
		else if (!strcasecmp(argv[args], "frames"))
			err = vgsm_me_cli_debug_frames(fd, me, enable);
		else if (!strcasecmp(argv[args], "sim"))
			err = vgsm_me_cli_debug_sim(fd, me, enable);
		else {
			ast_cli(fd, "Unrecognized category '%s'\n",
					argv[args]);
			err = RESULT_SHOWUSAGE;
		}

		if (me)
			vgsm_me_put(me);
	}

	if (err)
		return err;

	return RESULT_SUCCESS;
}

static int vgsm_me_cli_debug_func(int fd, int argc, char *argv[])
{
	return vgsm_me_cli_debug_do(fd, argc, argv, 3, TRUE);
}

static int vgsm_me_cli_no_debug_func(int fd, int argc, char *argv[])
{
	return vgsm_me_cli_debug_do(fd, argc, argv, 4, FALSE);
}

static char *vgsm_me_cli_debug_category_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int state)
{
	char *commands[] = { "state", "call", "atcommands", "serial", "sms",
				"cbm", "jitbuf", "frames", "sim" };
	int i;

	for(i=state; i<ARRAY_SIZE(commands); i++) {
		if (!strncasecmp(word, commands[i], strlen(word)))
			return strdup(commands[i]);
	}

	return NULL;
}

static char *vgsm_me_cli_debug_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{

	switch(pos) {
	case 3:
		return vgsm_me_cli_debug_category_complete(line, word, state);
	case 4:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char *vgsm_me_cli_no_debug_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{

	switch(pos) {
	case 4:
		return vgsm_me_cli_debug_category_complete(line, word, state);
	case 5:
		return vgsm_me_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_me_cli_debug_help[] =
"Usage: vgsm me debug [<state | call | atcommands | serial | sms | cbm |\n"
"			 jitbuf | frames> [me]]\n"
"\n"
"	Debug vGSM's me-related events\n"
"\n"
"	state		ME state transitions\n"
"	call		Call-related messages\n"
"	atcommands	AT-commands sent or received on the serial port\n"
"	serial		low-level serial communication, including line buffer\n"
"			and read()/write() calls. Caution: It can be very\n"
"			verbose.\n"
"	sms		SMS-DELIVER and SMS-STATUS-REPORT messages\n"
"	cbm		Cell-broadcast messages\n"
"	jitbuf		Audio jitter buffer\n"
"	frames		Audio frames\n";

static struct ast_cli_entry vgsm_me_cli_debug =
{
	{ "vgsm", "me", "debug", NULL },
	vgsm_me_cli_debug_func,
	"Enable ME debugging",
	vgsm_me_cli_debug_help,
	vgsm_me_cli_debug_complete
};

static struct ast_cli_entry vgsm_me_cli_no_debug =
{
	{ "vgsm", "me", "no", "debug", NULL },
	vgsm_me_cli_no_debug_func,
	"Disable ME debugging",
	NULL,
	vgsm_me_cli_no_debug_complete
};
#endif

/*---------------------------------------------------------------------------*/

int vgsm_me_load(void)
{
	ast_cli_register(&vgsm_me_show);

	ast_cli_register(&vgsm_me_cli_power);
	ast_cli_register(&vgsm_me_cli_reset);
	ast_cli_register(&vgsm_me_cli_service);
	ast_cli_register(&vgsm_me_cli_identify);
	ast_cli_register(&vgsm_me_cli_operator);
	ast_cli_register(&vgsm_me_cli_pin_set);
	ast_cli_register(&vgsm_me_cli_pin_input);
	ast_cli_register(&vgsm_me_cli_puk_input);
	ast_cli_register(&vgsm_me_cli_sms_send);
	ast_cli_register(&vgsm_me_cli_forwarding);
	ast_cli_register(&vgsm_me_cli_rawcommand);

#ifdef DEBUG_CODE
	ast_cli_register(&vgsm_me_cli_debug);
	ast_cli_register(&vgsm_me_cli_no_debug);
#endif

	return 0;
}

int vgsm_me_unload(void)
{
#ifdef DEBUG_CODE
	ast_cli_unregister(&vgsm_me_cli_no_debug);
	ast_cli_unregister(&vgsm_me_cli_debug);
#endif

	ast_cli_unregister(&vgsm_me_cli_rawcommand);
	ast_cli_unregister(&vgsm_me_cli_forwarding);
	ast_cli_unregister(&vgsm_me_cli_sms_send);
	ast_cli_unregister(&vgsm_me_cli_puk_input);
	ast_cli_unregister(&vgsm_me_cli_pin_input);
	ast_cli_unregister(&vgsm_me_cli_pin_set);
	ast_cli_unregister(&vgsm_me_cli_operator);
	ast_cli_unregister(&vgsm_me_cli_identify);
	ast_cli_unregister(&vgsm_me_cli_service);
	ast_cli_unregister(&vgsm_me_cli_reset);
	ast_cli_unregister(&vgsm_me_cli_power);

	ast_cli_unregister(&vgsm_me_show);

	return 0;
}

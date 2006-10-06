/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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

#include "../config.h"

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

#include <linux/vgsm.h>

//#include <linux/visdn/streamport.h>
//#include <linux/visdn/router.h>

#include "util.h"
#include "chan_vgsm.h"
#include "module.h"
#include "comm.h"
#include "causes.h"
#include "sms.h"
#include "cbm.h"
#include "operators.h"
#include "pin.h"

#define FAILED_RETRY_TIME (30 * SEC)
#define READY_UPDATE_TIME (30 * SEC)
#define CLOSED_POSTPONE (3 * SEC)
#define POWERING_ON_TIMEOUT (8 * SEC)
#define POWERING_OFF_TIMEOUT (7 * SEC)
#define WAITING_INITIALIZATION_DELAY (2 * SEC)

void vgsm_module_config_default(struct vgsm_module_config *mc)
{
	strcpy(mc->context, "vgsm");
	strcpy(mc->pin, "");

	mc->rx_gain = 255;
	mc->tx_gain = 255;
	mc->set_clock = 0;
	mc->poweroff_on_exit = TRUE;
	mc->operator_selection = VGSM_OPSEL_AUTOMATIC;

	strcpy(mc->operator_id, "");
	strcpy(mc->sms_service_center, "");
	strcpy(mc->sms_sender_domain, "localhost");
	strcpy(mc->sms_recipient_address, "root@localhost");

	mc->dtmf_quelch = FALSE;
	mc->dtmf_mutemax = FALSE;
	mc->dtmf_relax = FALSE;
}

static const char *vgsm_module_status_to_text(enum vgsm_module_status status)
{
	switch(status) {
	case VGSM_MODULE_STATUS_CLOSED:
		return "CLOSED";
	case VGSM_MODULE_STATUS_OFF:
		return "OFF";
	case VGSM_MODULE_STATUS_POWERING_ON:
		return "POWERING_ON";
	case VGSM_MODULE_STATUS_POWERING_OFF:
		return "POWERING_OFF";
	case VGSM_MODULE_STATUS_WAITING_INITIALIZATION:
		return "WAITING_INITIALIZATION";
	case VGSM_MODULE_STATUS_INITIALIZING:
		return "INITIALIZING";
	case VGSM_MODULE_STATUS_READY:
		return "READY";
	case VGSM_MODULE_STATUS_WAITING_SIM:
		return "WAITING_SIM";
	case VGSM_MODULE_STATUS_WAITING_PIN:
		return "WAITING_PIN";
	case VGSM_MODULE_STATUS_FAILED:
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

static const char *vgsm_module_operator_selection_to_text(
	enum vgsm_operator_selection selection)
{
	switch(selection) {
	case VGSM_OPSEL_AUTOMATIC:
		return "Automatic";
	case VGSM_OPSEL_MANUAL_UNLOCKED:
		return "Manual unlocked";
	case VGSM_OPSEL_MANUAL_FALLBACK:
		return "Manual with fallback";
	case VGSM_OPSEL_MANUAL_LOCKED:
		return "Manual locked";
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

const char *vgsm_module_error_to_text(int code)
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

struct vgsm_module_config *vgsm_module_config_alloc(void)
{
	struct vgsm_module_config *module_config;

	module_config = malloc(sizeof(*module_config));
	if (!module_config)
		return NULL;

	memset(module_config, 0, sizeof(*module_config));

	module_config->refcnt = 1;

	return module_config;
}

struct vgsm_module_config *vgsm_module_config_get(
	struct vgsm_module_config *module_config)
{
	assert(module_config);
	assert(module_config->refcnt > 0);
	assert(module_config->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	module_config->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return module_config;
}

void vgsm_module_config_put(struct vgsm_module_config *module_config)
{
	assert(module_config);
	assert(module_config->refcnt > 0);
	assert(module_config->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --module_config->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt)
		free(module_config);
}

static int vgsm_module_config_from_var(
	struct vgsm_module_config *mc,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "device")) { 
		strncpy(mc->device_filename, var->value,
			sizeof(mc->device_filename));
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
	} else if (!strcasecmp(var->name, "operator_selection")) {
		if (!strcasecmp(var->value, "auto"))
			mc->operator_selection = VGSM_OPSEL_AUTOMATIC;
		else if (!strcasecmp(var->value, "manual_unlocked"))
			mc->operator_selection = VGSM_OPSEL_MANUAL_UNLOCKED;
		else if (!strcasecmp(var->value, "manual_fallback"))
			mc->operator_selection = VGSM_OPSEL_MANUAL_FALLBACK;
		else if (!strcasecmp(var->value, "manual_locked"))
			mc->operator_selection = VGSM_OPSEL_MANUAL_LOCKED;
		else
			ast_log(LOG_WARNING,
				"Operator selection '%s' unknown\n",
				var->value);
	} else if (!strcasecmp(var->name, "operator_id")) {
		strncpy(mc->operator_id, var->value,
			sizeof(mc->operator_id));
	} else if (!strcasecmp(var->name, "sms_service_center")) { 
		strncpy(mc->sms_service_center, var->value,
			sizeof(mc->sms_service_center));
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
	} else {
		return -1;
	}

	return 0;
}

static void vgsm_module_config_copy(
	struct vgsm_module_config *dst,
	const struct vgsm_module_config *src)
{
	strncpy(dst->device_filename, src->device_filename,
		sizeof(dst->device_filename));
	strncpy(dst->context, src->context,
		sizeof(dst->context));
	strncpy(dst->pin, src->pin,
		sizeof(dst->pin));

	dst->rx_gain = src->rx_gain;
	dst->tx_gain = src->tx_gain;
	dst->set_clock = src->set_clock;
	dst->poweroff_on_exit = src->poweroff_on_exit;
	dst->operator_selection = src->operator_selection;

	strncpy(dst->operator_id, src->operator_id,
		sizeof(dst->operator_id));

	strncpy(dst->sms_service_center, src->sms_service_center,
		sizeof(dst->sms_service_center));
	strncpy(dst->sms_sender_domain, src->sms_sender_domain,
		sizeof(dst->sms_sender_domain));
	strncpy(dst->sms_recipient_address, src->sms_recipient_address,
		sizeof(dst->sms_recipient_address));

	dst->dtmf_quelch = src->dtmf_quelch;
	dst->dtmf_mutemax = src->dtmf_mutemax;
	dst->dtmf_relax = src->dtmf_relax;
}

static void vgsm_module_reconfigure(
	struct ast_config *cfg,
	const char *name)
{
	/* Allocate a new module configuration */

	struct vgsm_module_config *mc;
	mc = vgsm_module_config_alloc();
	if (!mc) {
		ast_log(LOG_ERROR, "Cannot allocate new module config %s\n",
			name);
		goto err_module_config_alloc;
	}

	vgsm_module_config_copy(mc, vgsm.default_mc);

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)name);
	while (var) {
		if (vgsm_module_config_from_var(mc, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}
		
		var = var->next;
	}

	ast_mutex_lock(&vgsm.lock);

	struct vgsm_module *module = vgsm_module_get_by_name(name);
	if (!module) {
		module = vgsm_module_alloc();
		if (!module) {
			ast_log(LOG_ERROR, "Cannot allocate new module %s\n",
				name);

			ast_mutex_unlock(&vgsm.lock);
			goto err_module_alloc;
		}

		strncpy(module->name, name, sizeof(module->name));

		vgsm_comm_init(&module->comm, vgsm_module_urcs);

		list_add_tail(&module->ifs_node, &vgsm.ifs);

		vgsm_module_set_status(module,
				VGSM_MODULE_STATUS_CLOSED,
				CLOSED_POSTPONE);
	}

	ast_mutex_lock(&module->lock);
	if (module->current_config) {
		vgsm_module_config_put(module->current_config);
		module->current_config = NULL;
	}

	mc->module = module;
	module->current_config = vgsm_module_config_get(mc);
	ast_mutex_unlock(&module->lock);

	ast_mutex_unlock(&vgsm.lock);

	vgsm_module_config_put(mc);

	return;

	vgsm_module_put(module);
err_module_alloc:
	vgsm_module_config_put(mc);
err_module_config_alloc:

	return;
}

void vgsm_module_reload(struct ast_config *cfg)
{
	/* Read default interface configuration */

	struct ast_variable *var;
	var = ast_variable_browse(cfg, "global");
	while (var) {
		if (vgsm_module_config_from_var(vgsm.default_mc, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general") ||
		    !strcasecmp(cat, "global"))
			continue;

		vgsm_module_reconfigure(cfg, cat);
	}
}

struct vgsm_module *vgsm_module_alloc(void)
{
	struct vgsm_module *module;

	module = malloc(sizeof(*module));
	if (!module)
		return NULL;

	memset(module, 0, sizeof(*module));

	module->refcnt = 1;

	ast_mutex_init(&module->lock);

	INIT_LIST_HEAD(&module->counters);

	module->status = VGSM_MODULE_STATUS_CLOSED;
	module->timer_expiration = -1;

	module->monitor_thread = AST_PTHREADT_NULL;

	return module;
}

struct vgsm_module *vgsm_module_get(struct vgsm_module *module)
{
	assert(module);
	assert(module->refcnt > 0);
	assert(module->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	module->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return module;
}

void vgsm_module_put(struct vgsm_module *module)
{
	assert(module);
	assert(module->refcnt > 0);
	assert(module->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --module->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		if (module->lockdown_reason)
			free(module->lockdown_reason);

		struct vgsm_counter *counter;
		list_for_each_entry(counter, &module->counters, node)
			free(counter);

		free(module);
	}
}

struct vgsm_module *vgsm_module_get_by_name(const char *name)
{
	ast_mutex_lock(&vgsm.lock);
	struct vgsm_module *module;
	list_for_each_entry(module, &vgsm.ifs, ifs_node) {

		if (!strcasecmp(module->name, name)) {
			ast_mutex_unlock(&vgsm.lock);
			return vgsm_module_get(module);
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

void vgsm_module_set_status(
	struct vgsm_module *module,
	enum vgsm_module_status status,
	longtime_t timeout)
{
	ast_mutex_lock(&module->lock);

	if (timeout >= 0) {
		module->timer_expiration = longtime_now() + timeout;

		vgsm_debug_generic(
			"vGSM module '%s' changed state from %s to %s"
			" (timeout %.2fs)\n",
			module->name,
			vgsm_module_status_to_text(module->status),
			vgsm_module_status_to_text(status),
			timeout / 1000000.0);
	} else {
		module->timer_expiration = -1;

		vgsm_debug_generic(
			"vGSM module '%s' changed state from %s to %s\n",
			module->name,
			vgsm_module_status_to_text(module->status),
			vgsm_module_status_to_text(status));
	}

	module->status = status;

	if (module->monitor_thread != AST_PTHREADT_NULL)
		pthread_kill(module->monitor_thread, SIGURG);

	ast_mutex_unlock(&module->lock);
}

void vgsm_module_set_status_reason(
	struct vgsm_module *module,
	enum vgsm_module_status status,
	longtime_t timeout,
	const char *fmt, ...)
{
	va_list ap;

	ast_mutex_lock(&module->lock);

	if (module->lockdown_reason)
		free(module->lockdown_reason);

	va_start(ap, fmt);
	vasprintf(&module->lockdown_reason, fmt, ap);
	va_end(ap);

	vgsm_module_set_status(module, status, timeout);

	ast_mutex_unlock(&module->lock);
}

void vgsm_module_unexpected_error(struct vgsm_module *module, int err)
{
	vgsm_comm_disable(&module->comm);

	ast_mutex_lock(&module->lock);

	if (err == VGSM_RESP_FAILED)
		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Communication error");
	else
		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Unexpected error: '%s'",
			vgsm_module_error_to_text(err));

	ast_mutex_unlock(&module->lock);
}

char *vgsm_module_completion(char *line, char *word, int state)
{
	int which = 0;

	ast_mutex_lock(&vgsm.lock);
	struct vgsm_module *module;
	list_for_each_entry(module, &vgsm.ifs, ifs_node) {
		if (!strncasecmp(word, module->name, strlen(word)) &&
		    ++which > state) {
			ast_mutex_unlock(&vgsm.lock);
			return strdup(module->name);
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

static void vgsm_module_ignite(
	struct vgsm_module *module)
{
	if (ioctl(module->comm.fd, VGSM_IOC_POWER_IGN, 0) < 0) {

		vgsm_comm_disable(&module->comm);

		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Error turning on module: ioctl(POWER_IGN): %s",
			strerror(errno));
	}
}

static void vgsm_module_emerg_off(
	struct vgsm_module *module)
{
	if (ioctl(module->comm.fd, VGSM_IOC_POWER_EMERG_OFF, 0) < 0) {

		vgsm_comm_disable(&module->comm);

		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Error turning off module: ioctl(POWER_EMERG_OFF): %s",
			strerror(errno));
	}
}

static void vgsm_show_module(int fd, struct vgsm_module *module)
{
	ast_mutex_lock(&module->lock);

	struct vgsm_module_config *mc = module->current_config;

	ast_cli(fd, "\n------ Interface '%s' ---------\n", module->name);

	ast_cli(fd,
		"\n"
	      	"  Device : %s\n"
		"  Context: %s\n"
		"  RX-gain: %d\n"
		"  TX-gain: %d\n"
		"  Set clock: %s\n"
		"  Power off on exit: %s\n",
		mc->device_filename,
		mc->context,
		mc->rx_gain,
		mc->tx_gain,
		mc->set_clock ? "YES" : "NO",
		mc->poweroff_on_exit ? "YES" : "NO");

	ast_cli(fd,
		"\nModule:\n"
		"  Status: %s\n",
		vgsm_module_status_to_text(module->status));

	switch(module->status) {
	case VGSM_MODULE_STATUS_CLOSED:
	case VGSM_MODULE_STATUS_OFF:
	case VGSM_MODULE_STATUS_POWERING_ON:
	case VGSM_MODULE_STATUS_POWERING_OFF:
	case VGSM_MODULE_STATUS_INITIALIZING:
	case VGSM_MODULE_STATUS_WAITING_INITIALIZATION:
		goto out;

	case VGSM_MODULE_STATUS_FAILED:
	case VGSM_MODULE_STATUS_WAITING_SIM:
	case VGSM_MODULE_STATUS_WAITING_PIN:
		ast_cli(fd, "  Reason: %s\n", module->lockdown_reason);
		goto out;

	case VGSM_MODULE_STATUS_READY:
	break;
	}

	ast_cli(fd,
		"  Model: %s %s\n"
		"  Version: %s\n"
		"  IMEI: %s\n",
		module->module.vendor,
		module->module.model,
		module->module.version,
		module->module.imei);

	/* Voltage */
	struct vgsm_req *req;
	req = vgsm_req_make_wait(&module->comm, 5 * SEC, "AT^SBV");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put_null(req);
		goto out;
	}

	const char *line = vgsm_req_first_line(req)->text;

	if (strlen(line) > strlen("^SBV: ")) {
		ast_cli(fd, "  Supply voltage: %d mV\n",
			atoi(line + strlen("^SBV: ")));
	}

	vgsm_req_put_null(req);

	/* Current */
	req = vgsm_req_make_wait(&module->comm, 5 * SEC, "AT^SBC?");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put_null(req);
		goto out;
	}

	line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("^SBC: ");
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		goto out;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		goto out;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		goto out;
	}

	ast_cli(fd, "  Power consumption: %d mA\n", atoi(field));

	vgsm_req_put_null(req);

	if (module->sim.inserted) {
		ast_cli(fd,
			"\nSIM:\n"
			"  Card ID: %s\n"
			"  IMSI: %s\n"
			"  PIN remaining attempts: %d\n",
			module->sim.card_id,
			module->sim.imsi,
			module->sim.remaining_attempts);
	} else {
		ast_cli(fd,
			"\nSIM:\n"
			"  Not inserted\n");
	}

	ast_cli(fd,
		"\nNetwork: \n"
		"  Operator Selection: %s\n",
		vgsm_module_operator_selection_to_text(
			mc->operator_selection));

	if (mc->operator_selection != VGSM_OPSEL_AUTOMATIC) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(mc->operator_id);

		if (op_info) {
			ast_cli(fd,
				"  Desidered network:"
				" %s (%s - %s - %s)\n",
				mc->operator_id,
				op_info->name,
				op_info->country,
				op_info->bands);
		} else {
			ast_cli(fd,
				"  Desidered network: %s)\n",
				mc->operator_id);
		}
	}

	ast_cli(fd,
		"  Status: %s\n",
		vgsm_net_status_to_text(module->net.status));

	if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
            module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING)
		goto out;

	struct vgsm_operator_info *op_info;
	op_info = vgsm_operators_search(module->net.operator_id);

	if (op_info) {
		ast_cli(fd,
			"  Current network:"
			" %s (%s - %s - %s)\n",
			module->net.operator_id,
			op_info->name,
			op_info->country,
			op_info->bands);
	} else {
		ast_cli(fd,
			"  Current network: %s\n",
			module->net.operator_id);
	}

	ast_cli(fd, "\nServing cell\n");
	ast_cli(fd,
		"  MCC MNC  LAC   ID"
		" BSIC ARFCN     RxLev\n");

	ast_cli(fd,
		"  %03d  %02d %04x %04x %4d %5d"
		" %5d dBm\n",
		module->net.sci.mcc,
		module->net.sci.mnc,
		module->net.sci.lac,
		module->net.sci.id,
		module->net.sci.bsic,
		module->net.sci.arfcn,
		-module->net.sci.rx_lev);

	ast_cli(fd,
		"  RxLev Sub: %d dBm\n"
		"  RxLev Full: %d dBm\n"
		"  RxQual: %d (BER %s)\n"
		"  RxQual Sub: %d (BER %s)\n"
		"  RxQual Full: %d (BER %s)\n"
		"  Timeslot: %d\n"
		"  TA: %d\n",
		-module->net.sci2.rx_lev_sub,
		-module->net.sci2.rx_lev_full,
		module->net.sci2.rx_qual,
		vgsm_qual_to_text(module->net.sci2.rx_qual),
		module->net.sci2.rx_qual_sub,
		vgsm_qual_to_text(module->net.sci2.rx_qual_sub),
		module->net.sci2.rx_qual_full,
		vgsm_qual_to_text(module->net.sci2.rx_qual_full),
		module->net.sci2.timeslot,
		module->net.sci2.ta);

	if (module->net.sci2.rssi == 0)
		ast_cli(fd, "  RSSI: <= -113 dBm\n");
	else if (module->net.sci2.rssi == 31)
		ast_cli(fd, "  RSSI: >= -51 dB,\n");
	else if (module->net.sci2.rssi == 99)
		ast_cli(fd, "  RSSI: N/A\n");
	else
		ast_cli(fd, "  RSSI: %d dBm\n",
			-113 + (module->net.sci2.rssi * 2));

	ast_cli(fd, "  BER: %d (%s)\n",
		module->net.sci2.ber,
		vgsm_qual_to_text(module->net.sci2.ber));

	if (module->net.ncells) {
		ast_cli(fd, "\nAdjacent cells (%d)\n",
			module->net.ncells);

		ast_cli(fd,
			"  #  MCC MNC  LAC   ID"
			" BSIC ARFCN     RxLev\n");

		int i;
		for (i=0; i<module->net.ncells; i++) {
			ast_cli(fd,
				" %2d: %03d  %02d %04x %04x %4d %5d"
				" %5d dBm\n",
				i + 1,
				module->net.nci[i].mcc,
				module->net.nci[i].mnc,
				module->net.nci[i].lac,
				module->net.nci[i].id,
				module->net.nci[i].bsic,
				module->net.nci[i].arfcn,
				-module->net.nci[i].rx_lev);
		}
	}

	ast_cli(fd, "\nCalls:\n");
	ast_cli(fd, "  #  State      Dir T Bearer          Channel\n");

	int i;
	for(i=0; i<ARRAY_SIZE(module->calls); i++) {
		if (module->calls[i].state == VGSM_CALL_STATE_UNUSED)
			continue;

		ast_cli(fd, "  %1d: %-10s %-3s %c %-15s %s\n",
			i + 1,
			vgsm_call_state_to_text(module->calls[i].state),
			(module->calls[i].direction ==
				VGSM_CALL_DIRECTION_MOBILE_TERMINATED ?
					"IN" : "OUT"),
			module->calls[i].channel_assigned ? '*' : ' ',
			vgsm_call_bearer_to_text(module->calls[i].bearer),
			(i == 0 && module->vgsm_chan) ?
				module->vgsm_chan->ast_chan->name : "");
	}

	ast_cli(fd, "\nDisconnect causes:\n");
	struct vgsm_counter *counter;
	list_for_each_entry(counter, &module->counters, node) {
		ast_cli(fd, "  %s/%s: %d\n",
			vgsm_cause_location_to_text(counter->location),
			vgsm_cause_reason_to_text(counter->location,
				counter->reason),
			counter->count);
	}

	ast_cli(fd, "\n");

out:
	ast_mutex_unlock(&module->lock);
}

static void vgsm_show_module_summary(int fd, struct vgsm_module *module)
{
	ast_mutex_unlock(&module->lock);

	ast_cli(fd, "%-10s: %s",
		module->name,
		vgsm_module_status_to_text(module->status));

	if (module->status == VGSM_MODULE_STATUS_READY) {
		ast_cli(fd, " %-17s",
			vgsm_net_status_to_text(module->net.status));

		if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	            module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {

			struct vgsm_operator_info *op_info;
			op_info = vgsm_operators_search(
					module->net.operator_id);

			ast_cli(fd, " \"%s\"",
				op_info ? op_info->name :
					module->net.operator_id);
		}
	}

	if (module->vgsm_chan)
		ast_cli(fd, " - CALL[%s]", module->vgsm_chan->ast_chan->name);

	if (module->sending_sms)
		ast_cli(fd, " - SENDING_SMS");

	ast_cli(fd, "\n");

	ast_mutex_unlock(&module->lock);
}

static int do_show_vgsm_modules(int fd, int argc, char *argv[])
{
	struct vgsm_module *module;

	if (argc >= 4) {
		module = vgsm_module_get_by_name(argv[3]);
		if (!module) {
			ast_cli(fd, "Module %s not found\n", argv[3]);
			return RESULT_FAILURE;
		}

		vgsm_show_module(fd, module);

	} else {
		ast_mutex_lock(&vgsm.lock);
		list_for_each_entry(module, &vgsm.ifs, ifs_node)
			vgsm_show_module_summary(fd, module);
		ast_mutex_unlock(&vgsm.lock);
	}

	return RESULT_SUCCESS;
}

static char *show_vgsm_modules_complete(
		char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word,state);
	}

	return NULL;
}

static char show_vgsm_modules_help[] =
"Usage: show vgsm modules\n"
"	Displays informations on vGSM modules\n";

static struct ast_cli_entry show_vgsm_modules =
{
	{ "show", "vgsm", "modules", NULL },
	do_show_vgsm_modules,
	"Displays vGSM module information",
	show_vgsm_modules_help,
	show_vgsm_modules_complete,
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_power_on(int fd, struct vgsm_module *module)
{
	ast_mutex_lock(&module->lock);

	if (module->status != VGSM_MODULE_STATUS_OFF) {
		ast_cli(fd, "Interface is not off, cannot turn on\n");
	} else {
		vgsm_module_ignite(module);

		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_POWERING_ON,
			POWERING_ON_TIMEOUT,
			"CLI request");
	}

	ast_mutex_unlock(&module->lock);

	return RESULT_SUCCESS;
}

static int do_vgsm_power_off(int fd, struct vgsm_module *module)
{
	struct vgsm_comm *comm = &module->comm;

	vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_POWERING_OFF,
			POWERING_OFF_TIMEOUT,
			"CLI request");

	int res = vgsm_req_make_wait_result(comm, 3 * SEC, "AT^SMSO");
	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Error: %s (%d)\n",
			vgsm_module_error_to_text(res),
			res);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int do_vgsm_power(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 3) {
		ast_cli(fd, "Missing module name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_module_name;
	}

	if (argc < 4) {
		ast_cli(fd, "Missing command\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_command;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(argv[2]);
	if (!module) {
		ast_cli(fd, "Cannot find module '%s'\n", argv[2]);
		err = RESULT_FAILURE;
		goto err_module_not_found;
	}

	if (!strcasecmp(argv[3], "on")) {
		err = do_vgsm_power_on(fd, module);
		if (err != RESULT_SUCCESS)
			goto err_power_on;
	} else if (!strcasecmp(argv[3], "off")) {
		err = do_vgsm_power_off(fd, module);
		if (err != RESULT_SUCCESS)
			goto err_power_off;
	} else {
		ast_cli(fd, "Unknown command '%s'\n", argv[3]);
		err = RESULT_SHOWUSAGE;
		goto err_unknown_command;
	}

	vgsm_module_put_null(module);

	return RESULT_SUCCESS;

err_unknown_command:
err_power_off:
err_power_on:
	vgsm_module_put_null(module);
err_module_not_found:
err_no_command:
err_no_module_name:

	return err;
}

static char vgsm_power_help[] =
	"Usage: vgsm power <module> <on|off>\n"
	"	Power on or off the specified module\n";

static char *vgsm_power_complete(char *line, char *word, int pos, int state)
{
	char *commands[] = { "on", "off" };
	int i;

	switch(pos) {
	case 2:
		return vgsm_module_completion(line, word, state);
	case 3:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	}

	return NULL;
}

static struct ast_cli_entry vgsm_power =
{
	{ "vgsm", "power", NULL },
	do_vgsm_power,
	"Power on the specified module",
	vgsm_power_help,
	vgsm_power_complete
};

/*---------------------------------------------------------------------------*/

#define to_module(cm) container_of((cm), struct vgsm_module, comm)

static void vgsm_module_counter_inc(
	struct vgsm_module *module,
	int location,
	int reason)
{
	ast_mutex_lock(&module->lock);

	struct vgsm_counter *counter;
	list_for_each_entry(counter, &module->counters, node) {
		if (counter->location == location &&
		    counter->reason == reason)
			goto found;
	}

	counter = malloc(sizeof(*counter));
	if (!counter)
		goto err_malloc;

	counter->location = location;
	counter->reason = reason;
	counter->count = 0;

	list_add(&counter->node, &module->counters);
	
found:
	counter++;

	ast_mutex_unlock(&module->lock);

	return;

err_malloc:

	return;
}

static int vgsm_request_ceer(struct vgsm_module *module)
{
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&module->comm, 5 * SEC, "AT+CEER");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_cerr;
	}

	const char *pars_ptr =
		vgsm_req_first_line(req)->text + strlen("+CEER: ");
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_cerr;

	int location = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_cerr;

	int reason = atoi(field);

	vgsm_req_put_null(req);

//	if (!get_token(&pars_ptr, field, sizeof(field)))
//		goto done;
//
//	int ssreason = atoi(field);

	vgsm_module_counter_inc(module, location, reason);

	ast_log(LOG_NOTICE,
		"Call released, location '%s', cause '%s'\n",
		vgsm_cause_location_to_text(location),
		vgsm_cause_reason_to_text(location, reason));

	return vgsm_cause_to_ast_cause(location, reason);

err_cerr:
	vgsm_req_put_null(req);

	return AST_CAUSE_NORMAL_CLEARING;
}


static void vgsm_common_hangup(struct vgsm_module *module)
{
	int cause;

	cause = vgsm_request_ceer(module);

	ast_mutex_lock(&module->lock);
	struct vgsm_chan *vgsm_chan = vgsm_chan_get(module->vgsm_chan);
	ast_mutex_unlock(&module->lock);

	if (vgsm_chan) {
		vgsm_chan->ast_chan->hangupcause = cause;
		ast_softhangup(vgsm_chan->ast_chan, AST_SOFTHANGUP_DEV);

		vgsm_chan_put(vgsm_chan);
	}
}

static void handle_unsolicited_no_carrier(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);

	vgsm_common_hangup(module);
}

static void handle_unsolicited_no_dialtone(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);

	vgsm_common_hangup(module);
}

static void handle_unsolicited_busy(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);

	vgsm_common_hangup(module);
}

static void handle_unsolicited_ring(
	const struct vgsm_req *urc)
{
	ast_log(LOG_NOTICE, "Unexpected RING\n");
}

static void handle_unsolicited_cring(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&module->lock);

	if (module->status != VGSM_MODULE_STATUS_READY) {
		ast_log(LOG_NOTICE,
			"Rejecting RING on not ready module\n");
		int err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_module_error_to_text(err), err);
			// FIXME TODO
		}

		goto err_module_not_ready;
	}

	if (strcmp(pars, "VOICE")) {
		ast_log(LOG_NOTICE, "Not a voice call, rejecting\n");

		int err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK) {
			// FIXME TODO
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_module_error_to_text(err), err);
		}

		goto err_not_voice;
	}

	if (module->vgsm_chan) {
		ast_log(LOG_WARNING,
			"Received +CRING with an already active call\n");
		goto err_call_already_present;
	}

	module->vgsm_chan = vgsm_alloc_inbound_call(module);
	if (!module->vgsm_chan) {
		vgsm_req_put(vgsm_req_make(&module->comm, 5 * SEC, "AT+CHUP"));

		goto err_alloc_call;
	}

	ast_mutex_unlock(&module->lock);

	return;

	vgsm_chan_put(module->vgsm_chan);
	ast_hangup(module->vgsm_chan->ast_chan);
	module->vgsm_chan = NULL;
err_alloc_call:
err_call_already_present:
err_not_voice:
err_module_not_ready:

	ast_mutex_unlock(&module->lock);

	return;
}

static void vgsm_update_module_by_creg(
	struct vgsm_module *module,
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

	switch(atoi(field)) {
	case 0: module->net.status = VGSM_NET_STATUS_NOT_SEARCHING; break;
	case 1: module->net.status = VGSM_NET_STATUS_REGISTERED_HOME; break;
	case 2: module->net.status = VGSM_NET_STATUS_NOT_REGISTERED; break;
	case 3: module->net.status = VGSM_NET_STATUS_REGISTRATION_DENIED; break;
	case 4: module->net.status = VGSM_NET_STATUS_UNKNOWN; break;
	case 5: module->net.status = VGSM_NET_STATUS_REGISTERED_ROAMING; break;
	}

	if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		// Update Net Info FIXME TODO
	}

	return;

err_no_status:
err_no_mode:

	return;
}

static int vgsm_update_cops(
	struct vgsm_module *module)
{
	int err;

	struct vgsm_req *req;

	req = vgsm_req_make_wait(&module->comm, 180 * SEC, "AT+COPS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
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
	if (!get_token(&line, module->net.operator_id,
			sizeof(module->net.operator_id)))
		goto parsing_complete;

parsing_complete:
	vgsm_req_put_null(req);

	return 0;

err_cops:

	return -1;
}

static void handle_unsolicited_creg(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&module->lock);

	vgsm_update_module_by_creg(module, pars, FALSE);

	vgsm_debug_generic(
		"Module '%s' registration %s\n",
		module->name,
		vgsm_net_status_to_text(module->net.status));

	if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING)
		vgsm_update_cops(module);

	ast_mutex_unlock(&module->lock);
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
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	ast_mutex_lock(&module->lock);

	if (!module->vgsm_chan) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_WARNING, "Received +CLIP without an active call\n");

		goto err_call_not_present;
	}

	struct vgsm_chan *vgsm_chan = vgsm_chan_get(module->vgsm_chan);
	struct ast_channel *ast_chan = vgsm_chan->ast_chan;

	ast_mutex_unlock(&module->lock);

	if (ast_chan->_state != AST_STATE_RING) {

		ast_log(LOG_WARNING,
			"Received +CLIP but active call"
			" is not in RING state\n");

		goto err_call_not_ring;
	}

	ast_chan->cid.cid_pres =
		AST_PRES_USER_NUMBER_UNSCREENED |
		AST_PRES_UNAVAILABLE;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
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

static int handle_cmt_end(
	const struct vgsm_req *urc)
{
	return TRUE;
}

static int handle_cbm_end(
	const struct vgsm_req *urc)
{
	return TRUE;
}

static void handle_unsolicited_cmt(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
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

	struct vgsm_sms *sms;
	sms = vgsm_decode_sms_pdu(line2->text);
	if (!sms)
		goto err_decode_failed;

	sms->module = module;

	if (vgsm_sms_spool(sms) >= 0) {

		/* Send Acknowledgment */

		int err = vgsm_req_make_wait_result(comm,
				20 * SEC, "AT+CNMA=0");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error acknowledging SMS: %s (%d)\n",
				vgsm_module_error_to_text(err), err);
			goto err_acknowledge;
		}
	}

	vgsm_sms_put(sms);

	return;

err_acknowledge:
err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static void handle_unsolicited_cbm(
	const struct vgsm_req *urc)
{
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

	struct vgsm_cbm *cbm = vgsm_decode_cbm_pdu(line2->text);
	if (!cbm)
		goto err_decode_failed;

	vgsm_cbm_put(cbm);

	return;

err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static void handle_unsolicited_cds(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
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

	struct vgsm_sms *sms;
	sms = vgsm_decode_sms_pdu(line2->text);
	if (!sms)
		goto err_decode_failed;

	int err = vgsm_req_make_wait_result(comm, 20 * SEC, "AT+CNMA=0");
	if (err != VGSM_RESP_OK) {
		ast_log(LOG_ERROR,
			"Error acknowledging SMS: %s (%d)\n",
			vgsm_module_error_to_text(err), err);
		goto err_acknowledge;
	}

	return;

err_acknowledge:
err_decode_failed:
err_missing_line2:
err_parse_length:

	return;
}

static void handle_unsolicited_cdsi(
	const struct vgsm_req *urc)
{
	ast_log(LOG_ERROR, "Unexptected CMTI!\n");
}

static void vgsm_handle_slcc_update(
	struct vgsm_module *module,
	struct vgsm_call *call)
{
	ast_mutex_lock(&module->lock);
	struct vgsm_chan *vgsm_chan = vgsm_chan_get(module->vgsm_chan);
	ast_mutex_unlock(&module->lock);

	switch(call->state) {
	case VGSM_CALL_STATE_UNUSED:

		if (vgsm_chan && vgsm_chan->ast_chan->pbx) {
			ast_log(LOG_NOTICE,
				"Call disappeared from SLCC,"
				" requesting HANGUP\n");

			ast_softhangup(vgsm_chan->ast_chan, AST_SOFTHANGUP_DEV);
        	}
	break;

	case VGSM_CALL_STATE_ACTIVE: {
		if (!vgsm_chan) {
			ast_log(LOG_ERROR, "Call is active but there is no"
				" current call\n");
			vgsm_req_put(vgsm_req_make(&module->comm,
					5 * SEC, "AT+CHUP"));

			break;
		}

		if (vgsm_chan->ast_chan->_state != AST_STATE_UP) {
			ast_setstate(vgsm_chan->ast_chan, AST_STATE_UP);
			ast_queue_control(vgsm_chan->ast_chan,
						AST_CONTROL_ANSWER);
		}
	}
	break;

	case VGSM_CALL_STATE_HELD:
		ast_log(LOG_DEBUG, "Unsupported state 1-held\n");
	break;

	case VGSM_CALL_STATE_DIALING:
		/* Do nutting */
	break;

	case VGSM_CALL_STATE_ALERTING: {
		if (!vgsm_chan) {
			ast_log(LOG_ERROR,
				"Call is alerting but there is no"
				" current call\n");

			vgsm_req_put(vgsm_req_make(&module->comm,
						5 * SEC, "AT+CHUP"));

			break;
		}

		if (vgsm_chan->ast_chan->_state != AST_STATE_RINGING) {
			ast_setstate(vgsm_chan->ast_chan, AST_STATE_RINGING);
			ast_queue_control(vgsm_chan->ast_chan,
							AST_CONTROL_RINGING);
		}
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
		if (vgsm_chan)
			ast_queue_control(vgsm_chan->ast_chan,
					AST_CONTROL_DISCONNECT);
	break;
	}

	vgsm_chan_put(vgsm_chan);
}

static void handle_unsolicited_ciev_battchg(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Battery level: %s\n", pars);
}

static void handle_unsolicited_ciev_signal(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Signal: %s\n", pars);
}

static void handle_unsolicited_ciev_service(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Service: %s\n", pars);
}

static void handle_unsolicited_ciev_sounder(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Sounder: %s\n", pars);
}

static void handle_unsolicited_ciev_message(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Message: %s\n", pars);
}

static void handle_unsolicited_ciev_call(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SLCC");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		return;
	}

	ast_mutex_lock(&module->lock);

	int i;
	for (i=0; i<ARRAY_SIZE(module->calls); i++)
		module->calls[i].updated = FALSE;

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node) {
		const char *pars = line->text + strlen("^SLCC: ");
		struct vgsm_call call;

		if (!strcmp(line->text, "OK"))
			break;

		if (strncmp(line->text, "^SLCC: ", 7)) {
			ast_log(LOG_ERROR,
				"Unexpected response %s to AT^SLCC\n",
				line->text);
			continue;
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
		} else if (idx >= ARRAY_SIZE(module->calls)) {
			ast_log(LOG_ERROR, "SLCC describes call index %d but"
				" a maximum of %d calls is handled\n",
				idx, ARRAY_SIZE(module->calls));
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

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' number\n",
				line->text);
			continue;
		}

		if (module->calls[idx].state != VGSM_CALL_STATE_UNUSED) {
			/* This is a new call */
		}

		module->calls[idx].updated = TRUE;
		module->calls[idx].direction = call.direction;
		module->calls[idx].state = call.state;
		module->calls[idx].bearer = call.bearer;
		module->calls[idx].multiparty = call.multiparty;
		module->calls[idx].channel_assigned = call.channel_assigned;
	}

	vgsm_req_put_null(req);

	for (i=0; i<ARRAY_SIZE(module->calls); i++) {
		if (!module->calls[i].updated) {
			module->calls[i].state = VGSM_CALL_STATE_UNUSED;
			/* Call removed */
		}
	}

	ast_mutex_unlock(&module->lock);

	/* There is a race condition here! module->calls[0] may change! FIXME */
	vgsm_handle_slcc_update(module, &module->calls[0]);
}

static void handle_unsolicited_ciev_roam(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Roaming: %s\n", pars);
}

static void handle_unsolicited_ciev_smsfull(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("SMS memory full: %s\n", pars);
}

static int vgsm_module_update_common_cell_info(
	struct vgsm_module *module,
	struct vgsm_net_cell *cell,
	const char **pars_ptr)
{
	char field[32];

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse MCC '%s'\n", *pars_ptr);
		goto err_moni;
	}
	
	sscanf(field, "%d", &cell->mcc);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse MNC '%s'\n", *pars_ptr);
		goto err_moni;
	}
	
	sscanf(field, "%d", &cell->mnc);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse LAC '%s'\n", *pars_ptr);
		goto err_moni;
	}
	
	sscanf(field, "%x", &cell->lac);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CID '%s'\n", *pars_ptr);
		goto err_moni;
	}
	
	sscanf(field, "%x", &cell->id);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse BSIC '%s'\n", *pars_ptr);
		goto err_moni;
	}

	sscanf(field, "%d", &cell->bsic);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse ARFCN '%s'\n", *pars_ptr);
		goto err_moni;
	}

	sscanf(field, "%d", &cell->arfcn);

	if (!get_token(pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLev '%s'\n", *pars_ptr);
		goto err_moni;
	}
	
	sscanf(field, "%d", &cell->rx_lev);

	return 0;

err_moni:

	return -1;
}

static int vgsm_update_smond(struct vgsm_module *module)
{
	struct vgsm_comm *comm = &module->comm;
	int err;

	ast_mutex_lock(&module->lock);

	module->net.ncells = 0;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SMOND");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_moni;
	}

	if (strlen(vgsm_req_first_line(req)->text) < strlen("^SMOND:")) {
		vgsm_req_put_null(req);
		goto err_moni;
	}

	const char *line;
	line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("^SMOND:");
	char field[32];

	if (vgsm_module_update_common_cell_info(module, &module->net.sci,
			       				&pars_ptr) < 0)
		goto err_moni;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevFull '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.rx_lev_full);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevSub '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.rx_lev_sub);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQual '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.rx_qual_full);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualFull '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.rx_qual_sub);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualSub '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.timeslot);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse Timeslot '%s'\n", line);
		goto err_moni;
	}

	int i;
	for (i=0; i<6; i++) {
		if (vgsm_module_update_common_cell_info(module,
				&module->net.nci[module->net.ncells],
				&pars_ptr) < 0)
			goto err_moni;

		if (module->net.nci[module->net.ncells].mnc != 0)
			module->net.ncells++;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse TA '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.ta);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RSSI '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.rssi);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse BER '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &module->net.sci2.ber);

	vgsm_req_put_null(req);

	ast_mutex_unlock(&module->lock);

	return 0;
	
err_moni:

	ast_mutex_unlock(&module->lock);

	return -1;
}

static void handle_unsolicited_ciev_rssi(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	
	vgsm_update_smond(module);
}

static void handle_unsolicited_ciev_audio(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Audio: %s\n", pars);
}

static void handle_unsolicited_ciev_vmwait1(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Voicemail 1 waiting: %s\n", pars);
}

static void handle_unsolicited_ciev_vmwait2(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Voicemail 2 waiting: %s\n", pars);
}

static void handle_unsolicited_ciev_ciphcall(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Ciphercall: %s\n", pars);
}

static void handle_unsolicited_ciev_eons(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Enhanced Operator Name String: %s\n", pars);
}

static void handle_unsolicited_ciev_nitz(
	const struct vgsm_req *urc,
	const char *pars)
{
	vgsm_debug_generic("Network Identity and Time Zone: %s\n", pars);
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
	struct vgsm_module *module = to_module(comm);

	vgsm_debug_generic("Module started (^SYSSTART received)\n");

	vgsm_module_set_status(module,
		VGSM_MODULE_STATUS_WAITING_INITIALIZATION,
		WAITING_INITIALIZATION_DELAY);
}

static void handle_unsolicited_shutdown(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);

	vgsm_debug_generic("Module powered off (^SHUTDOWN received)\n");

	vgsm_module_set_status(module, VGSM_MODULE_STATUS_OFF, -1);
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
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&module->lock);
	if (atoi(pars)) {
		vgsm_module_set_status(module,
			VGSM_MODULE_STATUS_WAITING_INITIALIZATION,
			WAITING_INITIALIZATION_DELAY);
		module->sim.inserted = TRUE;
	} else {
		vgsm_module_set_status(module, VGSM_MODULE_STATUS_WAITING_SIM, -1);
		module->sim.inserted = FALSE;
	}
	ast_mutex_unlock(&module->lock);
}

static void handle_unsolicited_sbc(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_log(LOG_ERROR,
		"%s: Power supply: %s\n", pars, module->name);
}

static void handle_unsolicited_sstn(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sctm_a(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Battery is under critical low temperature limit"
			" and shutting down\n", module->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Battery is under lower temperature limit\n",
			module->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Battery temperature is now ok\n",
			module->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Battery is over high temperature limit\n",
			module->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Battery is over critical high temperature limit"
			" and shutting down\n", module->name);
	break;
	}
}

static void handle_unsolicited_sctm_b(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_module *module = to_module(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Engine is under critical low temperature limit"
			" and shutting down\n", module->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Engine is under lower temperature limit\n",
			module->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Engine temperature is now ok\n",
			module->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Engine is over high temperature limit\n",
			module->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Engine is over critical high temperature limit"
			" and shutting down\n", module->name);
	break;
	}
}

struct vgsm_urc_class vgsm_module_urcs[] =
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
	{ "+CDS: ", handle_unsolicited_cds, NULL },
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

static int vgsm_module_pin_check_and_input(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_comm *comm = &module->comm;
	struct vgsm_req *req;
	const struct vgsm_req_line *first_line;
	int err;
	int res = 0;

	/* Be careful to not consume all the available attempts */
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SPIC");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_SIM, -1,
				"SIM not present");
		vgsm_req_put_null(req);
		goto err_spic;
	} else if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_spic;
	}

	module->sim.remaining_attempts =
		atoi(vgsm_req_first_line(req)->text + strlen("^SPIC: "));

	vgsm_req_put_null(req);

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_SIM, -1,
				"SIM not present");
		vgsm_req_put_null(req);
		goto err_spic;
	} else if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_spic;
	}

	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		/* Do nothing */
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		if (module->sim.remaining_attempts < 3) {
			vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_PIN, -1,
				"Input PIN manually");
			res = -1;
		} else if (strlen(mc->pin)) {
			int err = vgsm_req_make_wait_result(comm, 20 * SEC,
					"AT+CPIN=\"%s\"", mc->pin);

			if (err != VGSM_RESP_OK) {
				vgsm_module_set_status_reason(module,
					VGSM_MODULE_STATUS_WAITING_PIN, -1,
					"SIM PIN refused (%s), input manually",
					vgsm_module_error_to_text(err));
				res = -1;
			}
		} else {
			vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_PIN, -1,
				"SIM PIN not configured, input manually");
			res = -1;
		}
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_PIN, -1,
				"SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_PIN, -1,
				"SIM requires PUK, input manually");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_WAITING_PIN, -1,
				"SIM requires PUK2");
		res = -1;
	} else {
		vgsm_comm_disable(&module->comm);
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
				"Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_req_put_null(req);

	return res;

err_spic:

	return -1;
}

static int vgsm_module_codec_init(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_codec_ctl cctl;

	cctl.parameter = VGSM_CODEC_RXGAIN;
	cctl.value = mc->rx_gain;

	if (ioctl(module->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(IOC_CODEC_SET, RXGAIN) failed: %s\n",
			strerror(errno));

		goto err_ioctl_rxgain;
	}

	cctl.parameter = VGSM_CODEC_TXGAIN;
	cctl.value = mc->tx_gain;

	if (ioctl(module->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
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

static int vgsm_module_prepin_configure(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_comm *comm = &module->comm;
	int err;

	/* Enable call list unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC,
			"AT^SCFG=\"URC/CallStatus/CIEV\",\"verbose\"");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set TE character set */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC,
			"AT+CSCS=\"GSM\"");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Sets current time on module */
	if (mc->set_clock) {
		struct tm *tm;
		time_t ct = time(NULL);

		tm = localtime(&ct);

		err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
			"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03ld\"",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			-(timezone / 3600) + tm->tm_isdst);
		if (err != VGSM_RESP_OK) {
			vgsm_module_unexpected_error(module, err);
			goto err_no_req;
		}
	}

	/* Select audio mode 5 */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SNFS=5");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Select audio path 1 */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SAIC=2,1,1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set audio input */
	err = vgsm_req_make_wait_result(comm, 10 * SEC, "AT^SNFI=2,32767");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Set audio output */
	err = vgsm_req_make_wait_result(comm, 10 * SEC,
			"AT^SNFO=2,4096,5792,8192,11584,32767,4,0");
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

	vgsm_module_unexpected_error(module, err);

	return -1;
}

static int vgsm_module_configure(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_comm *comm = &module->comm;
	int err;

	/* Configure operator selection */
	switch(mc->operator_selection) {
	case VGSM_OPSEL_AUTOMATIC:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=0,2");
	break;

	case VGSM_OPSEL_MANUAL_UNLOCKED:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=1,2,%s",
			mc->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_FALLBACK:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=4,2,%s",
			mc->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_LOCKED:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=5,2,%s",
			mc->operator_id);
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
		vgsm_module_unexpected_error(module, err);

	/* Select message service  */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSMS=1");
	if (err != VGSM_RESP_OK)
		vgsm_module_unexpected_error(module, err);

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

	/* Disable call list unsolicited messages */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT^SLCC=0");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Enable unsolicited new message indications */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CNMI=2,2,2,1,1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	/* Subscribe to all cell broadcast channels */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSCB=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	sleep(2); // Let the module flush CBM

	/* Save the current configuration */
	err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT&W");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_module_unexpected_error(module, err);

	return -1;
}

static int vgsm_module_update_static_info(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_comm *comm = &module->comm;
	struct vgsm_req *req;
	int err;

	ast_mutex_lock(&module->lock);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(module->module.vendor,
		vgsm_req_first_line(req)->text,
		sizeof(module->module.vendor));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMM");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(module->module.model,
		vgsm_req_first_line(req)->text,
		sizeof(module->module.model));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMR");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(module->module.version,
		vgsm_req_first_line(req)->text,
		sizeof(module->module.version));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CGSN");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(module->module.imei,
		vgsm_req_first_line(req)->text,
		sizeof(module->module.imei));

	vgsm_req_put_null(req);

	
	/*--------*/
	req = vgsm_req_make_wait(comm, 100 * MILLISEC, "AT^SCKS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	if (strlen(vgsm_req_first_line(req)->text) > 7) {
		const char *pars_ptr = vgsm_req_first_line(req)->text + 7;
		char field[32];

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SCKS '%s'\n",
				vgsm_req_first_line(req)->text);
			goto no_sim;
		}

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SCKS '%s'\n",
				vgsm_req_first_line(req)->text);
			goto no_sim;
		}

		if (atoi(field) == 1)
			module->sim.inserted = TRUE;
		else
			module->sim.inserted = FALSE;
	}

	vgsm_req_put_null(req);

	if (!module->sim.inserted)
		goto no_sim;
	
	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CIMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(module->sim.imsi,
		vgsm_req_first_line(req)->text,
		sizeof(module->sim.imsi));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CXXCID");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	if (strlen(vgsm_req_first_line(req)->text) < strlen("+CXXCID: "))
		goto err_failure;

	strncpy(module->sim.card_id,
		vgsm_req_first_line(req)->text + strlen("+CXXCID: "),
		sizeof(module->sim.card_id));

	vgsm_req_put_null(req);

no_sim:

	ast_mutex_unlock(&module->lock);

	return 0;

err_failure:

	ast_mutex_unlock(&module->lock);

	return -1;
}

static int vgsm_module_update_net_info(
	struct vgsm_module *module)
{
	int err;
	struct vgsm_comm *comm = &module->comm;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CREG?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_unexpected_error(module, err);
		vgsm_req_put_null(req);
		goto err_creg_read_response;
	}

	const char *line = vgsm_req_first_line(req)->text;

	if (strlen(line) > strlen("+CREG: "))
		vgsm_update_module_by_creg(module,
				line + strlen("+CREG: "), TRUE);

	vgsm_req_put_null(req);

	if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {

		err = vgsm_update_smond(module);
		if (err < 0)
			goto err_update_smond;

		err = vgsm_update_cops(module);
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

static int vgsm_module_init_at_interface(
	struct vgsm_module *module,
	struct vgsm_module_config *mc)
{
	struct vgsm_comm *comm = &module->comm;
	int err;

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT Z0 E1 V1 Q0 \\Q1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT+IPR=38400");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	err = vgsm_req_make_wait_result(comm, 200 * MILLISEC,
		"AT+CMEE=1");
	if (err != VGSM_RESP_OK)
		goto err_no_req;

	return 0;

err_no_req:

	vgsm_module_unexpected_error(module, err);

	return -1;
}

static void vgsm_module_open(
	struct vgsm_module *module)
{
	assert(module->comm.fd == -1);

	ast_mutex_lock(&module->lock);
	struct vgsm_module_config *mc;
	mc = vgsm_module_config_get(module->current_config);
	ast_mutex_unlock(&module->lock);

	module->comm.fd = open(mc->device_filename, O_RDWR);
	if (module->comm.fd < 0) {
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
				"Error opening device: open(%s): %s",
				mc->device_filename,
				strerror(errno));

		goto err_module_open;
	}

	module->comm.name = module->name;

	struct termios newtio;
	bzero(&newtio, sizeof(newtio));
	
	newtio.c_cflag = B38400 | CRTSCTS | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IXON | IXOFF;
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
	
	tcflush(module->comm.fd, TCIFLUSH);
	tcsetattr(module->comm.fd, TCSANOW, &newtio);

	bzero(&newtio, sizeof(newtio));
	
	newtio.c_cflag = B38400 | CRTSCTS | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IXON | IXOFF;
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
	
	tcflush(module->comm.fd, TCIFLUSH);
	tcsetattr(module->comm.fd, TCSANOW, &newtio);

	/***************/

	vgsm_comm_enable(&module->comm);

	int val;
	if (ioctl(module->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Error getting power status:"
			" ioctl(POWER_GET): %s",
			strerror(errno));

		goto err_ioctl_power_get;
	}

	if (val) {
		vgsm_debug_generic(
			"Module is already powered on, I'm not waiting"
			" for SYSSTART\n");

		vgsm_module_set_status(module,
				VGSM_MODULE_STATUS_WAITING_INITIALIZATION,
				0);
	} else {
		vgsm_module_ignite(module);

		/* The very first module power-up will not send ^SYSSTART URC
		 * because it is configured in auto-bauding mode. We will time
		 * out and initialize it anyway.
		 */
		module->power_attempts = 0;
		vgsm_module_set_status(module, VGSM_MODULE_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT);
	}

	vgsm_module_config_put(mc);

	return;

err_ioctl_power_get:
	vgsm_comm_disable(&module->comm);

	close(module->comm.fd);
err_module_open:
	vgsm_module_config_put(mc);

	return;
}

static void vgsm_module_initialize(
	struct vgsm_module *module)
{
	vgsm_debug_generic("Initializing module '%s'\n", module->name);

	ast_mutex_lock(&module->lock);
	struct vgsm_module_config *mc;
	mc = vgsm_module_config_get(module->current_config);
	ast_mutex_unlock(&module->lock);

	vgsm_module_set_status(module, VGSM_MODULE_STATUS_INITIALIZING, -1);

	if (vgsm_module_codec_init(module, mc) < 0) {
		vgsm_comm_disable(&module->comm);
		vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
				"Error configuring CODEC");

		goto initialization_failure;
	}

	int val;
	if (ioctl(module->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
		vgsm_module_set_status_reason(module,
			VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
			"Error getting power status: ioctl(POWER_GET): %s",
			strerror(errno));

		goto initialization_failure;
	}

	if (!val) {
		ast_log(LOG_NOTICE,
			"Module '%s' is not powered on, re-igniting\n",
			module->name);
		
		module->power_attempts = 0;
		vgsm_module_set_status(module, VGSM_MODULE_STATUS_POWERING_ON,
						POWERING_ON_TIMEOUT);

		vgsm_module_ignite(module);

		goto initialization_failure;
	}

	if (vgsm_module_init_at_interface(module, mc) < 0)
		goto initialization_failure;

	if (vgsm_module_prepin_configure(module, mc) < 0)
		goto initialization_failure;

	if (vgsm_module_pin_check_and_input(module, mc) < 0)
		goto initialization_failure;

	if (vgsm_module_configure(module, mc) < 0)
		goto initialization_failure;

	if (vgsm_module_update_static_info(module, mc) < 0)
		goto initialization_failure;

	if (vgsm_module_update_net_info(module) < 0)
		goto initialization_failure;

	vgsm_module_set_status(module, VGSM_MODULE_STATUS_READY,
						READY_UPDATE_TIME);

	vgsm_debug_generic("Module '%s' successfully initialized\n",
		module->name);

	vgsm_module_config_put(mc);

	return;

initialization_failure:

	vgsm_module_config_put(mc);

	/* If no-one changed the status to something significant fall back to
	 * FAILED
	 */

	if (module->status == VGSM_MODULE_STATUS_INITIALIZING)
		vgsm_module_set_status(module, VGSM_MODULE_STATUS_FAILED,
					FAILED_RETRY_TIME);
}

static void vgsm_module_monitor_timer(
	struct vgsm_module *module)
{
	switch(module->status) {
	case VGSM_MODULE_STATUS_CLOSED:
		vgsm_module_open(module);
	break;

	case VGSM_MODULE_STATUS_POWERING_ON: {
		int val;
		if (ioctl(module->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
			vgsm_module_set_status_reason(module,
				VGSM_MODULE_STATUS_FAILED, FAILED_RETRY_TIME,
				"Error getting power status:"
				" ioctl(POWER_GET): %s",
				strerror(errno));

			return;
		}

		if (val) {
			vgsm_debug_generic("Module '%s': SYSTART missed\n",
				module->name);

			vgsm_module_set_status(module,
				VGSM_MODULE_STATUS_WAITING_INITIALIZATION,
				0);
		} else {
			module->power_attempts++;
			if (module->power_attempts > 3) {
				ast_log(LOG_ERROR,
					"%s: Power-on permanently failed\n",
					module->name);

				vgsm_module_set_status_reason(module,
					VGSM_MODULE_STATUS_OFF, -1,
					"Power-on sequence failure");
			} else {
				ast_log(LOG_NOTICE,
					"%s: Power-on sequence failed,"
					" retrying\n",
					module->name);

				vgsm_module_set_status(module,
					VGSM_MODULE_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT);

				vgsm_module_ignite(module);
			}
		}
	}
	break;

	case VGSM_MODULE_STATUS_POWERING_OFF:
		vgsm_module_emerg_off(module);

		vgsm_module_set_status(module,
			VGSM_MODULE_STATUS_OFF, -1);
	break;

	case VGSM_MODULE_STATUS_FAILED:
		if (module->comm.fd >= 0) {
			ast_mutex_lock(&module->lock);
			close(module->comm.fd);
			module->comm.fd = -1;
			ast_mutex_unlock(&module->lock);
			vgsm_comm_wakeup(&module->comm);
		}

		vgsm_module_set_status(module, VGSM_MODULE_STATUS_CLOSED, 1 * SEC);
	break;

	case VGSM_MODULE_STATUS_WAITING_INITIALIZATION:
		vgsm_module_initialize(module);
	break;

	case VGSM_MODULE_STATUS_READY:
		vgsm_module_update_net_info(module);

		/* Re-arm timer */
		vgsm_module_set_status(module, VGSM_MODULE_STATUS_READY,
						READY_UPDATE_TIME);
	break;

	case VGSM_MODULE_STATUS_INITIALIZING:
	case VGSM_MODULE_STATUS_OFF:
		ast_log(LOG_ERROR,
			"vgsm: Module '%s': Unexpected timer in status %s\n",
			module->name,
			vgsm_module_status_to_text(module->status));
	break;

	case VGSM_MODULE_STATUS_WAITING_SIM:
	case VGSM_MODULE_STATUS_WAITING_PIN:
		// Do nothing
	break;
	}
}

static void *vgsm_module_monitor_thread_main(void *data)
{
	struct vgsm_module *module = (struct vgsm_module *)data;

	for(;;) {
		ast_mutex_lock(&module->lock);

		if (module->timer_expiration != -1) {
			longtime_t timeout = module->timer_expiration -
						longtime_now();
			if (timeout < 0) {
				module->timer_expiration = -1;
				ast_mutex_unlock(&module->lock);

				vgsm_module_monitor_timer(module);
			} else if (timeout > 100000000) {
				ast_mutex_unlock(&module->lock);

				sleep(timeout / 1000000);
			} else {
				ast_mutex_unlock(&module->lock);

				usleep(timeout);
			}
		} else {
			ast_mutex_unlock(&module->lock);

			sleep(3600);
		}
	}

	return NULL;
}

void vgsm_module_shutdown_all(void)
{
	struct vgsm_module *module;

	ast_verbose("vgsm: powering off all modules\n");

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(module, &vgsm.ifs, ifs_node) {

		ast_mutex_lock(&module->lock);

		if (module->current_config->poweroff_on_exit &&
		    module->status != VGSM_MODULE_STATUS_CLOSED &&
		    module->status != VGSM_MODULE_STATUS_POWERING_OFF &&
		    module->status != VGSM_MODULE_STATUS_OFF &&
		    module->comm.fd >= 0) {
			vgsm_module_set_status(module,
					VGSM_MODULE_STATUS_POWERING_OFF,
					POWERING_OFF_TIMEOUT);

			vgsm_req_put(vgsm_req_make(&module->comm,
						3 * SEC, "AT^SMSO"));
		}

		ast_mutex_unlock(&module->lock);
	}
	ast_mutex_unlock(&vgsm.lock);

	time_t begin = time(NULL);

	while(time(NULL) - begin < 10) {
		int not_off = FALSE;

		ast_mutex_lock(&vgsm.lock);
		list_for_each_entry(module, &vgsm.ifs, ifs_node) {

			ast_mutex_lock(&module->lock);
			if (module->current_config->poweroff_on_exit &&
			    module->status != VGSM_MODULE_STATUS_OFF)
				not_off = TRUE;

			ast_mutex_unlock(&module->lock);
		}
		ast_mutex_unlock(&vgsm.lock);

		if (!not_off)
			return;

		sleep(1);
	}

	ast_verbose("vgsm: Failure to power off all modules\n");
}

int vgsm_module_module_load(void)
{
	int err;

	if (vgsm_comm_thread_create() < 0) {
		ast_log(LOG_ERROR, "Unable to start communication thread.\n");
		err = -1;
		goto err_comm_thread_create;
	}
	
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	struct vgsm_module *module;
	list_for_each_entry(module, &vgsm.ifs, ifs_node) {
		ast_pthread_create(&module->monitor_thread, &attr,
			vgsm_module_monitor_thread_main, module);
	}

	ast_cli_register(&show_vgsm_modules);
	ast_cli_register(&vgsm_power);

	return 0;

err_comm_thread_create:
	ast_cli_unregister(&show_vgsm_modules);
	ast_cli_unregister(&vgsm_power);

	return err;
}

int vgsm_module_module_unload(void)
{
	ast_cli_unregister(&show_vgsm_modules);
	ast_cli_unregister(&vgsm_power);

	return 0;
}

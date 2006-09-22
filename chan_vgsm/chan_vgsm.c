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

#include <linux/visdn/streamport.h>
#include <linux/visdn/router.h>

#include "util.h"
#include "chan_vgsm.h"
#include "comm.h"
#include "causes.h"
#include "sms.h"
#include "cbm.h"

#define FAILED_RETRY_TIME (30 * SEC)
#define READY_UPDATE_TIME (30 * SEC)
#define CLOSED_POSTPONE (3 * SEC)
#define POWERING_ON_TIMEOUT (8 * SEC)
#define POWERING_OFF_TIMEOUT (7 * SEC)
#define WAITING_INITIALIZATION_DELAY (2 * SEC)

#define VGSM_DESCRIPTION "VoiSmart VGSM Channel For Asterisk"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"
#define VGSM_OP_CONFIG_FILE "vgsm_operators.conf"

struct vgsm_state vgsm = {
	.usecnt = 0,
#ifdef DEBUG_DEFAULTS
	.debug_generic = TRUE,
	.debug_serial = TRUE,
#else
	.debug_generic = FALSE,
	.debug_serial = FALSE,
#endif

	.default_intf = {
		.context = "vgsm",
		.pin = "",
		.rx_gain = 255,
		.tx_gain = 255,
		.set_clock = 0,
		.poweroff_on_exit = TRUE,
		.operator_selection = VGSM_OPSEL_AUTOMATIC,
		.operator_id = "",
		.sms_service_center = "",
		.sms_sender_domain = "localhost",
		.sms_recipient_address = "root@localhost",
		.dtmf_quelch = FALSE,
		.dtmf_mutemax = FALSE,
		.dtmf_relax = FALSE,
	}
};

static const char *vgsm_intf_status_to_text(enum vgsm_intf_status status)
{
	switch(status) {
	case VGSM_INTF_STATUS_CLOSED:
		return "CLOSED";
	case VGSM_INTF_STATUS_OFF:
		return "OFF";
	case VGSM_INTF_STATUS_POWERING_ON:
		return "POWERING_ON";
	case VGSM_INTF_STATUS_POWERING_OFF:
		return "POWERING_OFF";
	case VGSM_INTF_STATUS_WAITING_INITIALIZATION:
		return "WAITING_INITIALIZATION";
	case VGSM_INTF_STATUS_INITIALIZING:
		return "INITIALIZING";
	case VGSM_INTF_STATUS_READY:
		return "READY";
	case VGSM_INTF_STATUS_RING:
		return "RING";
	case VGSM_INTF_STATUS_INCALL:
		return "INCALL";
	case VGSM_INTF_STATUS_SENDING_SMS:
		return "SENDING_SMS";
	case VGSM_INTF_STATUS_INCALL_SENDING_SMS:
		return "INCALL_SENDING_SMS";
	case VGSM_INTF_STATUS_WAITING_SIM:
		return "WAITING_SIM";
	case VGSM_INTF_STATUS_WAITING_PIN:
		return "WAITING_PIN";
	case VGSM_INTF_STATUS_FAILED:
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


static const char *vgsm_intf_operator_selection_to_text(
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

static int vgsm_validate_pin(const char *pin)
{
	int i;

	for (i=0; i<strlen(pin); i++) {
		if (!isdigit(pin[i]))
			return -1;
	}

	return 0;
}

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

static const char *vgsm_error_to_text(int code)
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

static struct vgsm_interface *vgsm_intf_alloc(void)
{
	struct vgsm_interface *intf;

	intf = malloc(sizeof(*intf));
	if (!intf)
		return NULL;

	memset(intf, 0, sizeof(*intf));

	intf->refcnt = 1;

	ast_mutex_init(&intf->lock);

	INIT_LIST_HEAD(&intf->counters);

	intf->status = VGSM_INTF_STATUS_CLOSED;
	intf->timer_expiration = -1;

	intf->monitor_thread = AST_PTHREADT_NULL;

	return intf;
}

static struct vgsm_interface *vgsm_intf_get(
	struct vgsm_interface *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);
	assert(intf->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	intf->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return intf;
}

static void vgsm_intf_put(
	struct vgsm_interface *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);
	assert(intf->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --intf->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		if (intf->lockdown_reason)
			free(intf->lockdown_reason);

		struct vgsm_counter *counter;
		list_for_each_entry(counter, &intf->counters, node)
			free(counter);

		free(intf);
	}
}

static struct vgsm_interface *vgsm_intf_get_by_name(const char *name)
{
	ast_mutex_lock(&vgsm.lock);
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		if (!strcasecmp(intf->name, name)) {
			ast_mutex_unlock(&vgsm.lock);
			return vgsm_intf_get(intf);
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

struct vgsm_operator_info *vgsm_search_operator(const char *id)
{
	struct vgsm_operator_info *op_info;

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(op_info, &vgsm.op_list, node) {
		if (!strcmp(op_info->id, id)) {
			ast_mutex_unlock(&vgsm.lock);
			return op_info;
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

static void vgsm_intf_setreason(
	struct vgsm_interface *intf,
	const char *fmt, ...)
{
	va_list ap;

	if (intf->lockdown_reason)
		free(intf->lockdown_reason);

	va_start(ap, fmt);
	vasprintf(&intf->lockdown_reason, fmt, ap);
	va_end(ap);
}

static void vgsm_intf_set_status(
	struct vgsm_interface *intf,
	enum vgsm_intf_status status,
	longtime_t timeout)
{
	if (timeout >= 0) {
		intf->timer_expiration = longtime_now() + timeout;

		vgsm_debug_generic(
			"vGSM interface '%s' changed state from %s to %s"
			" (timeout %.2fs)\n",
			intf->name,
			vgsm_intf_status_to_text(intf->status),
			vgsm_intf_status_to_text(status),
			timeout / 1000000.0);
	} else {
		intf->timer_expiration = -1;

		vgsm_debug_generic(
			"vGSM interface '%s' changed state from %s to %s\n",
			intf->name,
			vgsm_intf_status_to_text(intf->status),
			vgsm_intf_status_to_text(status));
	}

	intf->status = status;

	if (intf->monitor_thread != AST_PTHREADT_NULL)
		pthread_kill(intf->monitor_thread, SIGURG);
}

static void vgsm_intf_unexpected_error(struct vgsm_interface *intf, int err)
{
	vgsm_comm_disable(&intf->comm);

	if (err == VGSM_RESP_FAILED)
		vgsm_intf_setreason(intf, "Communication error");
	else
		vgsm_intf_setreason(intf, "Unexpected error: '%s'",
			vgsm_error_to_text(err));

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED, FAILED_RETRY_TIME);
}

static char *vgsm_interface_completion(char *line, char *word, int state)
{
	int which = 0;

	ast_mutex_lock(&vgsm.lock);
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strncasecmp(word, intf->name, strlen(word)) &&
		    ++which > state) {
			ast_mutex_unlock(&vgsm.lock);
			return strdup(intf->name);
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return NULL;
}

//-----------------------------------------------------------------------------

static int do_vgsm_pin_set(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_interface;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing OLDPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_oldpin;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "OLDPIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_oldpin_invalid;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_newpin;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		err = RESULT_SHOWUSAGE;
		goto err_intf_not_found;
	}

	int res;
	if (!strcasecmp(argv[5], "enabled")) {
		res = vgsm_req_make_wait_result(&intf->comm, 180 * SEC,
			"AT+CLCK=SC,1,\"%s\"", argv[4]);
	} else if (!strcasecmp(argv[5], "disabled")) {
		res = vgsm_req_make_wait_result(&intf->comm, 180 * SEC,
			"AT+CLCK=SC,0,\"%s\"", argv[4]);
	} else {
		if (vgsm_validate_pin(argv[5]) < 0) {
			ast_cli(fd, "NEWPIN contains invalid characters\n");
			err = RESULT_FAILURE;
			goto err_newpin_invalid;
		}

		res = vgsm_req_make_wait_result(&intf->comm, 180 * SEC,
			"AT+CPWD=SC,\"%s\",\"%s\"",
			argv[4], argv[5]);
	}

	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Unable to complete command: %s (%d)\n",
			vgsm_error_to_text(res),
			res);
		err = RESULT_FAILURE;
		goto err_response;
	}

	vgsm_intf_put_null(intf);

	return RESULT_SUCCESS;

err_response:
err_newpin_invalid:
	vgsm_intf_put_null(intf);
err_intf_not_found:
err_missing_newpin:
err_oldpin_invalid:
err_missing_oldpin:
err_missing_interface:

	return err;
}

static char *vgsm_pin_set_complete(char *line, char *word, int pos, int state)
{
	char *commands[] = { "enabled", "disabled" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_interface_completion(line, word, state);
	case 5:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_pin_set_help[] =
"Usage: vgsm pin set <interface> <OLDPIN> <NEWPIN|enabled|disabled>\n"
"	Set, enable or disable the PIN on the SIM installed in module \n"
"	<interface>.\n";

static struct ast_cli_entry vgsm_pin_set =
{
	{ "vgsm", "pin", "set", NULL },
	do_vgsm_pin_set,
	"Set PIN on the selected interface",
	vgsm_pin_set_help,
	vgsm_pin_set_complete,
};

//-----------------------------------------------------------------------------

static int vgsm_state_from_var(
	struct vgsm_state *state,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "sms_spooler")) { 
		strncpy(state->sms_spooler, var->value,
			sizeof(state->sms_spooler));
	} else if (!strcasecmp(var->name, "sms_spooler_pars")) {
		strncpy(state->sms_spooler_pars, var->value,
			sizeof(state->sms_spooler_pars));
	} else {
		return -1;
	}

	return 0;
}

static int vgsm_intf_from_var(
	struct vgsm_interface *intf,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "device")) { 
		strncpy(intf->device_filename, var->value,
			sizeof(intf->device_filename));
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(intf->context, var->value, sizeof(intf->context));
	} else if (!strcasecmp(var->name, "pin")) {
		strncpy(intf->pin, var->value, sizeof(intf->pin));
	} else if (!strcasecmp(var->name, "rx_gain")) {
		intf->rx_gain = atoi(var->value);
	} else if (!strcasecmp(var->name, "tx_gain")) {
		intf->tx_gain = atoi(var->value);
	} else if (!strcasecmp(var->name, "set_clock")) {
		intf->set_clock = ast_true(var->value);
	} else if (!strcasecmp(var->name, "poweroff_on_exit")) {
		intf->poweroff_on_exit = ast_true(var->value);
	} else if (!strcasecmp(var->name, "operator_selection")) {
		if (!strcasecmp(var->value, "auto"))
			intf->operator_selection = VGSM_OPSEL_AUTOMATIC;
		else if (!strcasecmp(var->value, "manual_unlocked"))
			intf->operator_selection = VGSM_OPSEL_MANUAL_UNLOCKED;
		else if (!strcasecmp(var->value, "manual_fallback"))
			intf->operator_selection = VGSM_OPSEL_MANUAL_FALLBACK;
		else if (!strcasecmp(var->value, "manual_locked"))
			intf->operator_selection = VGSM_OPSEL_MANUAL_LOCKED;
		else
			ast_log(LOG_WARNING,
				"Operator selection '%s' unknown\n",
				var->value);
	} else if (!strcasecmp(var->name, "operator_id")) {
		strncpy(intf->operator_id, var->value,
			sizeof(intf->operator_id));
	} else if (!strcasecmp(var->name, "sms_service_center")) { 
		strncpy(intf->sms_service_center, var->value,
			sizeof(intf->sms_service_center));
	} else if (!strcasecmp(var->name, "sms_sender_domain")) { 
		strncpy(intf->sms_sender_domain, var->value,
			sizeof(intf->sms_sender_domain));
	} else if (!strcasecmp(var->name, "sms_recipient_address")) { 
		strncpy(intf->sms_recipient_address, var->value,
			sizeof(intf->sms_recipient_address));
	} else if (!strcasecmp(var->name, "dtmf_quelch")) { 
		intf->dtmf_quelch = ast_true(var->value);
	} else if (!strcasecmp(var->name, "dtmf_mutemax")) { 
		intf->dtmf_mutemax = ast_true(var->value);
	} else if (!strcasecmp(var->name, "dtmf_relax")) { 
		intf->dtmf_relax = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void vgsm_copy_interface_config(
	struct vgsm_interface *dst,
	const struct vgsm_interface *src)
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

static void vgsm_module_ignite(
	struct vgsm_interface *intf)
{
	if (ioctl(intf->comm.fd, VGSM_IOC_POWER_IGN, 0) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_POWER_IGN) failed: %s\n",
			strerror(errno));
		vgsm_comm_disable(&intf->comm);
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
		vgsm_intf_setreason(intf, "Error turning on module");
	}
}

static void vgsm_module_emerg_off(
	struct vgsm_interface *intf)
{
	if (ioctl(intf->comm.fd, VGSM_IOC_POWER_EMERG_OFF, 0) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_POWER_EMERG_OFF) failed: %s\n",
			strerror(errno));
		vgsm_comm_disable(&intf->comm);
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
		vgsm_intf_setreason(intf, "Error turning off module");
	}
}

static struct vgsm_urc_class urc_classes[];

static void vgsm_reload_config(void)
{
	/* TODO FIXME XXX: Fix locking during reload */

	struct ast_config *cfg;
	cfg = ast_config_load(VGSM_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s, VGSM disabled\n",
			VGSM_CONFIG_FILE);

		return;
	}
	
	struct ast_variable *var;
	var = ast_variable_browse(cfg, "general");
	while (var) {
		if (vgsm_state_from_var(&vgsm, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	var = ast_variable_browse(cfg, "global");
	while (var) {
		if (vgsm_intf_from_var(&vgsm.default_intf, var) < 0) {
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

		int found = FALSE;
		struct vgsm_interface *intf;
		list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
			if (!strcasecmp(intf->name, cat)) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			intf = vgsm_intf_alloc();
			if (!intf) {
				// Do something FIXME
				return;
			}

printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA %d\n", vgsm.default_intf.poweroff_on_exit);

			strncpy(intf->name, cat, sizeof(intf->name));
			vgsm_copy_interface_config(intf, &vgsm.default_intf);

			vgsm_comm_init(&intf->comm, urc_classes);

			list_add_tail(&intf->ifs_node, &vgsm.ifs);

			vgsm_intf_set_status(intf, VGSM_INTF_STATUS_CLOSED,
						CLOSED_POSTPONE);
		}

		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (vgsm_intf_from_var(intf, var) < 0) {
				ast_log(LOG_WARNING,
					"Unknown configuration variable %s\n",
					var->name);
			}
			
			var = var->next;
		}
	}

	ast_config_destroy(cfg);

/*---------------------*/

	cfg = ast_config_load(VGSM_OP_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s: %s\n",
			VGSM_OP_CONFIG_FILE,
			strerror(errno));

		return;
	}

	struct vgsm_operator_info *op_info, *t;

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry_safe(op_info, t, &vgsm.op_list, node) {

		if (op_info->country)
			free(op_info->country);

		if (op_info->name)
			free(op_info->name);

		if (op_info->date)
			free(op_info->date);

		if (op_info->bands)
			free(op_info->bands);

		list_del(&op_info->node);
		free(op_info);
	}

	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		struct vgsm_operator_info *op_info;
		op_info = malloc(sizeof(*op_info));
		memset(op_info, 0, sizeof(*op_info));

		strncpy(op_info->id, cat, sizeof(op_info->id));

		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (!strcasecmp(var->name, "country"))
				op_info->country = strdup(var->value);
			else if (!strcasecmp(var->name, "name"))
				op_info->name = strdup(var->value);
			else if (!strcasecmp(var->name, "date"))
				op_info->date = strdup(var->value);
			else if (!strcasecmp(var->name, "bands"))
				op_info->bands = strdup(var->value);
			else {
				ast_log(LOG_WARNING,
					"Unknown parameter '%s' in %s\n",
					var->name,
					VGSM_OP_CONFIG_FILE);
			}
			
			var = var->next;
		}

		list_add_tail(&op_info->node, &vgsm.op_list);
	}

	ast_mutex_unlock(&vgsm.lock);
}

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_serial(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_serial = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_serial_help[] =
"Usage: debug vgsm serial\n"
"	Debug vGSM's serial communication\n";

static struct ast_cli_entry debug_vgsm_serial =
{
	{ "debug", "vgsm", "serial", NULL },
	do_debug_vgsm_serial,
	"Enables serial vGSM debugging",
	debug_vgsm_serial_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_serial(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_serial = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_serial =
{
	{ "no", "debug", "vgsm", "serial", NULL },
	do_no_debug_vgsm_serial,
	"Disables serial vGSM debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_generic = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_generic_help[] =
"Usage: debug vgsm generic\n"
"	Debug generic vGSM events\n";

static struct ast_cli_entry debug_vgsm_generic =
{
	{ "debug", "vgsm", "generic", NULL },
	do_debug_vgsm_generic,
	"Enables generic vGSM debugging",
	debug_vgsm_generic_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_generic = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_generic =
{
	{ "no", "debug", "vgsm", "generic", NULL },
	do_no_debug_vgsm_generic,
	"Disables generic vGSM debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

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

static const char *get_token(const char **s, char *token, int token_size)
{
	const char *p = *s;
	char *token_p = token;

	for(;;) {
		if (*p == '"') {
			p++;

			while(*p && *p != '"') {
				*token_p++ = *p++;

				if (token_p == token + token_size - 2)
					break;
			}

			if (*p == '"')
				p++;
		}

		if (!*p)
			break;

		if (*p == ',') {
			*s = p + 1;

			break;
		}

		*token_p++ = *p++;

		if (token_p == token + token_size - 2)
			break;
	}

	*token_p = '\0';

	return token;
}

static void vgsm_show_interface(int fd, struct vgsm_interface *intf)
{
	ast_cli(fd, "\n------ Interface '%s' ---------\n", intf->name);

	ast_cli(fd,
		"\n"
	      	"  Device : %s\n"
		"  Context: %s\n"
		"  RX-gain: %d\n"
		"  TX-gain: %d\n"
		"  Set clock: %s\n"
		"  Power off on exit: %s\n",
		intf->device_filename,
		intf->context,
		intf->rx_gain,
		intf->tx_gain,
		intf->set_clock ? "YES" : "NO",
		intf->poweroff_on_exit ? "YES" : "NO");

	ast_cli(fd,
		"\nModule:\n"
		"  Status: %s\n",
		vgsm_intf_status_to_text(intf->status));

	switch(intf->status) {
	case VGSM_INTF_STATUS_CLOSED:
	case VGSM_INTF_STATUS_OFF:
	case VGSM_INTF_STATUS_POWERING_ON:
	case VGSM_INTF_STATUS_POWERING_OFF:
	case VGSM_INTF_STATUS_INITIALIZING:
	case VGSM_INTF_STATUS_WAITING_INITIALIZATION:
		return;

	case VGSM_INTF_STATUS_FAILED:
	case VGSM_INTF_STATUS_WAITING_SIM:
	case VGSM_INTF_STATUS_WAITING_PIN:
		ast_cli(fd, "  Reason: %s\n", intf->lockdown_reason);
		return;

	case VGSM_INTF_STATUS_READY:
	case VGSM_INTF_STATUS_RING:
	case VGSM_INTF_STATUS_INCALL:
	case VGSM_INTF_STATUS_SENDING_SMS:
	case VGSM_INTF_STATUS_INCALL_SENDING_SMS:
	break;
	}

	ast_cli(fd,
		"  Model: %s %s\n"
		"  Version: %s\n"
		"  IMEI: %s\n",
		intf->module.vendor,
		intf->module.model,
		intf->module.version,
		intf->module.imei);

	/* Voltage */
	struct vgsm_req *req;
	req = vgsm_req_make_wait(&intf->comm, 5 * SEC, "AT^SBV");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put_null(req);
		return;
	}

	const char *line = vgsm_req_first_line(req)->text;

	if (strlen(line) > strlen("^SBV: ")) {
		ast_cli(fd, "  Supply voltage: %d mV\n",
			atoi(line + strlen("^SBV: ")));
	}

	vgsm_req_put_null(req);

	/* Current */
	req = vgsm_req_make_wait(&intf->comm, 5 * SEC, "AT^SBC?");
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put_null(req);
		return;
	}

	line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("^SBC: ");
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		return;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		return;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse SBC '%s'\n", line);
		vgsm_req_put_null(req);
		return;
	}

	ast_cli(fd, "  Power consumption: %d mA\n", atoi(field));

	vgsm_req_put_null(req);


	if (intf->sim.inserted) {
		ast_cli(fd,
			"\nSIM:\n"
			"  Card ID: %s\n"
			"  IMSI: %s\n"
			"  PIN remaining attempts: %d\n",
			intf->sim.card_id,
			intf->sim.imsi,
			intf->sim.remaining_attempts);
	} else {
		ast_cli(fd,
			"\nSIM:\n"
			"  Not inserted\n");
	}

	ast_cli(fd,
		"\nNetwork: \n"
		"  Operator Selection: %s\n",
		vgsm_intf_operator_selection_to_text(
			intf->operator_selection));

	if (intf->operator_selection != VGSM_OPSEL_AUTOMATIC) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_search_operator(intf->operator_id);

		if (op_info) {
			ast_cli(fd,
				"  Desidered network:"
				" %s (%s - %s - %s)\n",
				intf->operator_id,
				op_info->name,
				op_info->country,
				op_info->bands);
		} else {
			ast_cli(fd,
				"  Desidered network: %s)\n",
				intf->operator_id);
		}
	}

	ast_cli(fd,
		"  Status: %s\n",
		vgsm_net_status_to_text(intf->net.status));

	if (intf->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
            intf->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING)
		return;

	struct vgsm_operator_info *op_info;
	op_info = vgsm_search_operator(intf->net.operator_id);

	if (op_info) {
		ast_cli(fd,
			"  Current network:"
			" %s (%s - %s - %s)\n",
			intf->net.operator_id,
			op_info->name,
			op_info->country,
			op_info->bands);
	} else {
		ast_cli(fd,
			"  Current network: %s\n",
			intf->net.operator_id);
	}

	ast_cli(fd, "\nServing cell\n");
	ast_cli(fd,
		"  MCC MNC  LAC   ID"
		" BSIC ARFCN     RxLev\n");

	ast_cli(fd,
		"  %03d  %02d %04x %04x %4d %5d"
		" %5d dBm\n",
		intf->net.sci.mcc,
		intf->net.sci.mnc,
		intf->net.sci.lac,
		intf->net.sci.id,
		intf->net.sci.bsic,
		intf->net.sci.arfcn,
		-intf->net.sci.rx_lev);

	ast_cli(fd,
		"  RxLev Sub: %d dBm\n"
		"  RxLev Full: %d dBm\n"
		"  RxQual: %d (BER %s)\n"
		"  RxQual Sub: %d (BER %s)\n"
		"  RxQual Full: %d (BER %s)\n"
		"  Timeslot: %d\n"
		"  TA: %d\n",
		-intf->net.sci2.rx_lev_sub,
		-intf->net.sci2.rx_lev_full,
		intf->net.sci2.rx_qual,
		vgsm_qual_to_text(intf->net.sci2.rx_qual),
		intf->net.sci2.rx_qual_sub,
		vgsm_qual_to_text(intf->net.sci2.rx_qual_sub),
		intf->net.sci2.rx_qual_full,
		vgsm_qual_to_text(intf->net.sci2.rx_qual_full),
		intf->net.sci2.timeslot,
		intf->net.sci2.ta);

	if (intf->net.sci2.rssi == 0)
		ast_cli(fd, "  RSSI: <= -113 dBm\n");
	else if (intf->net.sci2.rssi == 31)
		ast_cli(fd, "  RSSI: >= -51 dB,\n");
	else if (intf->net.sci2.rssi == 99)
		ast_cli(fd, "  RSSI: N/A\n");
	else
		ast_cli(fd, "  RSSI: %d dBm\n",
			-113 + (intf->net.sci2.rssi * 2));

	ast_cli(fd, "  BER: %d (%s)\n",
		intf->net.sci2.ber,
		vgsm_qual_to_text(intf->net.sci2.ber));

	if (intf->net.ncells) {
		ast_cli(fd, "\nAdjacent cells (%d)\n",
			intf->net.ncells);

		ast_cli(fd,
			"  #  MCC MNC  LAC   ID"
			" BSIC ARFCN     RxLev\n");

		int i;
		for (i=0; i<intf->net.ncells; i++) {
			ast_cli(fd,
				" %2d: %03d  %02d %04x %04x %4d %5d"
				" %5d dBm\n",
				i + 1,
				intf->net.nci[i].mcc,
				intf->net.nci[i].mnc,
				intf->net.nci[i].lac,
				intf->net.nci[i].id,
				intf->net.nci[i].bsic,
				intf->net.nci[i].arfcn,
				-intf->net.nci[i].rx_lev);
		}
	}

	ast_cli(fd, "\nDisconnect causes:\n");
	struct vgsm_counter *counter;
	list_for_each_entry(counter, &intf->counters, node) {
		ast_cli(fd, "  %s/%s: %d\n",
			vgsm_cause_location_to_text(counter->location),
			vgsm_cause_reason_to_text(counter->location,
				counter->reason),
			counter->count);
	}
}

static void vgsm_show_interface_summary(int fd, struct vgsm_interface *intf)
{
	ast_cli(fd, "%-8s: %-8s\n",
		intf->name,
		vgsm_intf_status_to_text(intf->status));

	if (intf->status == VGSM_INTF_STATUS_CLOSED ||
	    intf->status == VGSM_INTF_STATUS_OFF ||
	    intf->status == VGSM_INTF_STATUS_POWERING_ON ||
	    intf->status == VGSM_INTF_STATUS_POWERING_OFF ||
	    intf->status == VGSM_INTF_STATUS_WAITING_INITIALIZATION ||
	    intf->status == VGSM_INTF_STATUS_INITIALIZING ||
	    intf->status == VGSM_INTF_STATUS_FAILED)
		return;

	if (intf->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
            intf->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_cli(fd, "          %-15s\n",
			vgsm_net_status_to_text(intf->net.status));
	} else {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_search_operator(intf->net.operator_id);

		if (op_info) {
			ast_cli(fd, "          %-15s %s (%s - %s - %s)\n",
				vgsm_net_status_to_text(intf->net.status),
				intf->net.operator_id,
				op_info->name,
				op_info->country,
				op_info->bands);
		} else {
			ast_cli(fd, "          %-15s %s\n",
				vgsm_net_status_to_text(intf->net.status),
				intf->net.operator_id);
		}
	}
}

static int do_show_vgsm_interfaces(int fd, int argc, char *argv[])
{
	struct vgsm_interface *intf;

	ast_mutex_lock(&vgsm.lock);
	if (argc >= 4) {
		list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
			if (!strcasecmp(intf->name, argv[3])) {
				vgsm_show_interface(fd, intf);
				break;
			}
		}
	} else {
		list_for_each_entry(intf, &vgsm.ifs, ifs_node)
			vgsm_show_interface_summary(fd, intf);
	}
	ast_mutex_unlock(&vgsm.lock);

	return RESULT_SUCCESS;
}

static char *show_vgsm_interfaces_complete(
		char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_interface_completion(line, word,state);
	}

	return NULL;
}

static char show_vgsm_interfaces_help[] =
"Usage: show vgsm interfaces\n"
"	Displays informations on vGSM interfaces\n";

static struct ast_cli_entry show_vgsm_interfaces =
{
	{ "show", "vgsm", "interfaces", NULL },
	do_show_vgsm_interfaces,
	"Displays vGSM interface information",
	show_vgsm_interfaces_help,
	show_vgsm_interfaces_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_number_parse(
	const char *num,
	char *addr, int addr_len,
	enum vgsm_numbering_plan *np,
	enum vgsm_type_of_number *ton)
{
	// FIXME TODO: Better validity checking

	assert(num);
	assert(addr);
	assert(np);
	assert(ton);

	*np = VGSM_NP_ISDN;

	if (num[0] == '+') {
		strncpy(addr, num + 1, addr_len);
		*ton = VGSM_TON_INTERNATIONAL;
	} else {
		strncpy(addr, num, addr_len);
		*ton = VGSM_TON_NATIONAL;
	}

	return 0;
}

static int do_vgsm_send_sms(int fd, int argc, char *argv[])
{
	int err = RESULT_SUCCESS;

	if (argc < 4) {
		ast_cli(fd, "Missing interface");
		err = RESULT_SHOWUSAGE;
		goto err_missing_intf;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing phone number");
		err = RESULT_SHOWUSAGE;
		goto err_missing_number;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing text");
		err = RESULT_SHOWUSAGE;
		goto err_missing_text;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_intf_not_found;
	}

	ast_mutex_lock(&intf->lock);
	if (intf->status == VGSM_INTF_STATUS_READY)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_SENDING_SMS, -1);
	else if (intf->status == VGSM_INTF_STATUS_INCALL)
		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_INCALL_SENDING_SMS,
				READY_UPDATE_TIME);
	else {
		ast_cli(fd,
			"Interface '%s' is busy\n",
			intf->name);
		ast_mutex_unlock(&intf->lock);
		err = RESULT_FAILURE;
		goto err_intf_not_ready;
	}
	ast_mutex_unlock(&intf->lock);

	struct vgsm_sms *sms;
	sms = vgsm_sms_alloc();
	if (!sms) {
		ast_cli(fd,  "Cannot allocate SMS\n");
		err = RESULT_FAILURE;
		goto err_sms_alloc;
	}

	sms->intf = intf;
	vgsm_number_parse(
		intf->sms_service_center,
		sms->smcc, sizeof(sms->smcc),
		&sms->smcc_np, &sms->smcc_ton);

	vgsm_number_parse(
		argv[4],
		sms->dest, sizeof(sms->dest),
		&sms->dest_np,
		&sms->dest_ton);

	sms->timestamp = time(NULL);

	tzset();
	sms->timezone = timezone;

	if (argc >= 7)
		sms->message_class = atoi(argv[6]);
	else
		sms->message_class = 1;

	size_t slen;
	slen = mbstowcs(NULL, argv[5], 0);
	if(slen == -1)
		goto err_invalid_mbstring;
 
	sms->text = malloc((slen + 1) * sizeof(wchar_t));
	if(!sms->text)
		goto err_malloc_sms_text;
 
	mbstowcs(sms->text, argv[5], slen);
	sms->text[slen] = L'\0';

	vgsm_sms_prepare(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&intf->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	if (!req) {
		ast_cli(fd, "Error sending SMS\n");
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	vgsm_req_wait(req);

	if (req->err != VGSM_RESP_OK) {
		ast_cli(fd, "Error sending SMS: %s (%d)\n",
			vgsm_error_to_text(req->err),
			req->err);
		goto err_req_result;
	}

	if (intf->status == VGSM_INTF_STATUS_SENDING_SMS)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
				READY_UPDATE_TIME);
	else if (intf->status == VGSM_INTF_STATUS_INCALL_SENDING_SMS)
		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_INCALL,
				READY_UPDATE_TIME);
 
	vgsm_req_put_null(req);

	return RESULT_SUCCESS;

err_req_result:
	vgsm_req_put_null(req);
err_req_make:
err_invalid_mbstring:
err_malloc_sms_text:
	vgsm_sms_put_null(sms);
err_sms_alloc:
	if (intf->status == VGSM_INTF_STATUS_SENDING_SMS)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
				READY_UPDATE_TIME);
	else if (intf->status == VGSM_INTF_STATUS_INCALL_SENDING_SMS)
		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_INCALL,
				READY_UPDATE_TIME);
err_intf_not_ready:
err_intf_not_found:
err_missing_text:
err_missing_number:
err_missing_intf:

	return err;
}

static char *vgsm_send_sms_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_interface_completion(line, word,state);
	}

	return NULL;
}

static char vgsm_send_sms_help[] =
	"Usage: vgsm send sms <intf> <number> <text> [class]\n"
	"	Send SMS\n";

static struct ast_cli_entry vgsm_send_sms =
{
	{ "vgsm", "send", "sms", NULL },
	do_vgsm_send_sms,
	"Send SMS",
	vgsm_send_sms_help,
	vgsm_send_sms_complete,
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_reload(int fd, int argc, char *argv[])
{
	vgsm_reload_config();

	return RESULT_SUCCESS;
}

static char vgsm_vgsm_reload_help[] =
	"Usage: vgsm reload\n"
	"	Reloads vGSM config\n";

static struct ast_cli_entry vgsm_reload =
{
	{ "vgsm", "reload", NULL },
	do_vgsm_reload,
	"Reloads vGSM configuration",
	vgsm_vgsm_reload_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_power(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 3) {
		ast_cli(fd, "Missing interface name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_interface_name;
	}

	if (argc < 4) {
		ast_cli(fd, "Missing command\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_command;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[2]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[2]);
		err = RESULT_FAILURE;
		goto err_intf_not_found;
	}

	struct vgsm_comm *comm = &intf->comm;

	if (!strcasecmp(argv[3], "on")) {

		vgsm_module_ignite(intf);

		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT);

	} else if (!strcasecmp(argv[3], "off")) {

		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_POWERING_OFF,
				POWERING_OFF_TIMEOUT);

		int res = vgsm_req_make_wait_result(comm, 3 * SEC, "AT^SMSO");
		if (res != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_error_to_text(res),
				res);
			err = RESULT_FAILURE;
			goto err_request_smso;
		}
	} else {
		ast_cli(fd, "Unknown command '%s'\n", argv[3]);
		err = RESULT_SHOWUSAGE;
		goto err_unknown_command;
	}

	vgsm_intf_put_null(intf);

	return RESULT_SUCCESS;

err_unknown_command:
err_request_smso:
	vgsm_intf_put_null(intf);
err_intf_not_found:
err_no_command:
err_no_interface_name:

	return err;
}

static char vgsm_power_help[] =
	"Usage: vgsm power <interface> <on|off>\n"
	"	Power on or off the specified module\n";

static char *vgsm_power_complete(char *line, char *word, int pos, int state)
{
	char *commands[] = { "on", "off" };
	int i;

	switch(pos) {
	case 2:
		return vgsm_interface_completion(line, word, state);
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

static int do_vgsm_pin_input(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_interface_name;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PIN\n");
		err =  RESULT_SHOWUSAGE;
		goto err_no_pin;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "PIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_pin_invalid;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_intf_not_found;
	}

	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	if (req->err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, req->err);
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

		int res = vgsm_req_make_wait_result(comm, 20 * SEC,
				"AT+CPIN=\"%s\"", argv[4]);
		if (res != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_error_to_text(res),
				res);
			err = RESULT_FAILURE;
			goto err_send_pin;
		}

		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_WAITING_INITIALIZATION, -1);

	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		ast_cli(fd, "SIM requires PUK");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);
		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put_null(req);
	vgsm_intf_put_null(intf);

	return RESULT_SUCCESS;

err_unknown_response:
err_send_pin:
err_not_waiting_pin:
	vgsm_req_put_null(req);
err_req_make:
	vgsm_intf_put_null(intf);
err_intf_not_found:
err_pin_invalid:
err_no_pin:
err_no_interface_name:

	return err;
}

static char *vgsm_pin_input_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_interface_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_pin_input_help[] =
	"Usage: vgsm pin input <interface> <PIN>\n"
	"	Manually input PIN to selected interface\n";

static struct ast_cli_entry vgsm_pin_input =
{
	{ "vgsm", "pin", "input", NULL },
	do_vgsm_pin_input,
	"Manually input PIN to selected interface",
	vgsm_pin_input_help,
	vgsm_pin_input_complete,
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_puk_input(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_intf_name;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PUK\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_puk;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "PUK contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_puk_invalid;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_newpin;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "NEWPIN contains invalid characters\n");
		err = RESULT_FAILURE;
		goto err_newpin_invalid;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_intf_not_found;
	}

	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
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
		ast_cli(fd, "SIM requires PIN");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {

		int err = vgsm_req_make_wait_result(comm, 20 * SEC,
				"AT+CPIN=\"%s\",\"%s\"", argv[4], argv[5]);
		if (err != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_error_to_text(err),
				err);
			err = RESULT_FAILURE;
		goto err_invalid_state;
		}

		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_WAITING_INITIALIZATION, -1);

	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);

		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put_null(req);
	vgsm_intf_put_null(intf);

	return RESULT_SUCCESS;

err_unknown_response:
err_invalid_state:
	vgsm_req_put_null(req);
err_req_make:
	vgsm_intf_put_null(intf);
err_intf_not_found:
err_newpin_invalid:
err_no_newpin:
err_puk_invalid:
err_no_puk:
err_no_intf_name:

	return err;
}

static char *vgsm_puk_input_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_interface_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_puk_input_help[] =
	"Usage: vgsm puk input <interface> <PUK>\n"
	"	Manually input PUK to selected interface\n";

static struct ast_cli_entry vgsm_puk_input =
{
	{ "vgsm", "puk", "input", NULL },
	do_vgsm_puk_input,
	"Manually input PUK to selected interface",
	vgsm_puk_input_help,
	vgsm_puk_input_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_connect_channel(
	struct vgsm_chan *vgsm_chan)
{
	if (ioctl(vgsm_chan->intf->comm.fd, VGSM_IOC_GET_CHANID,
		(caddr_t)&vgsm_chan->module_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VGSM_IOC_GET_CHANID): %s\n",
			strerror(errno));
		goto err_ioctl_vgsm_get_chanid;
	}

	vgsm_chan->sp_fd = open("/dev/visdn/streamport", O_RDWR);
	if (vgsm_chan->sp_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open streamport: %s\n",
			strerror(errno));
		goto err_open_streamport;
	}

	if (ioctl(vgsm_chan->sp_fd, VISDN_SP_GET_CHANID,
		(caddr_t)&vgsm_chan->sp_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_GET_CHANID): %s\n",
			strerror(errno));
		goto err_ioctl_visdn_get_chanid;
	}

	vgsm_debug_generic("Connecting streamport %06d to chan %06d\n",
			vgsm_chan->sp_channel_id,
			vgsm_chan->module_channel_id);

	struct visdn_connect vc;
	vc.src_chan_id = vgsm_chan->sp_channel_id;
	vc.dst_chan_id = vgsm_chan->module_channel_id;
	vc.flags = 0;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_CONNECT,
						(caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_CONNECT, sp, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_connect_pipeline;
	}

	vgsm_chan->pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->pipeline_id;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						(caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_OPEN, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_enable_pipeline;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->pipeline_id;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_PIPELINE_START,
						(caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_START, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_enable_pipeline;
	}

	return 0;

err_ioctl_enable_pipeline:
err_ioctl_connect_pipeline:
err_ioctl_visdn_get_chanid:
	close(vgsm_chan->sp_fd);
err_open_streamport:
err_ioctl_vgsm_get_chanid:

	return -1;
}

static void vgsm_destroy(struct vgsm_chan *vgsm_chan)
{
	free(vgsm_chan);
}

static struct vgsm_chan *vgsm_alloc()
{
	struct vgsm_chan *vgsm_chan;

	vgsm_chan = malloc(sizeof(*vgsm_chan));
	if (!vgsm_chan)
		return NULL;

	memset(vgsm_chan, 0, sizeof(*vgsm_chan));

	vgsm_chan->sp_fd = -1;

	return vgsm_chan;
}

static const struct ast_channel_tech vgsm_tech;

static struct ast_channel *vgsm_new(
	struct vgsm_chan *vgsm_chan,
	int state)
{
	struct ast_channel *ast_chan;
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}

	ast_chan->type = VGSM_CHAN_TYPE;

	ast_chan->fds[0] = open("/dev/visdn/timer", O_RDONLY);
	if (ast_chan->fds[0] < 0) {
		ast_log(LOG_ERROR, "Unable to open timer: %s\n",
			strerror(errno));
		goto err_open_timer;
	}

	if (state == AST_STATE_RING)
		ast_chan->rings = 1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;
	ast_chan->rawwriteformat = AST_FORMAT_ALAW;

	vgsm_chan->ast_chan = ast_chan;
	ast_chan->tech_pvt = vgsm_chan;
	ast_chan->tech = &vgsm_tech;

	vgsm_chan->dsp = ast_dsp_new();
	ast_dsp_set_features(vgsm_chan->dsp, DSP_FEATURE_DTMF_DETECT);

	ast_setstate(ast_chan, state);

	return ast_chan;

	close(ast_chan->fds[0]);
err_open_timer:
	ast_hangup(ast_chan);
err_channel_alloc:

	return NULL;
}

static struct ast_channel *vgsm_alloc_inbound_call(struct vgsm_interface *intf)
{
	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_alloc;
	}

	vgsm_chan->intf = intf;

	struct ast_channel *ast_chan;
	ast_chan = vgsm_new(vgsm_chan, AST_STATE_OFFHOOK);
	if (!ast_chan)
		goto err_vgsm_new;

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->intf->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->intf->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->intf->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	strcpy(ast_chan->exten, "s");
	strncpy(ast_chan->context, intf->context, sizeof(ast_chan->context));
	ast_chan->priority = 1;

	snprintf(ast_chan->name, sizeof(ast_chan->name),
		"VGSM/%s/%d", intf->name, 1);

	ast_setstate(ast_chan, AST_STATE_RING);

	return ast_chan;

err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:

	return NULL;
}

static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	int err;
	char dest[256];

	strncpy(dest, orig_dest, sizeof(dest));

	// Parse destination and obtain interface name + number
	const char *intf_name;
	const char *number;
	char *stringp = dest;

	intf_name = strsep(&stringp, "/");
	if (!intf_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_format;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(intf_name);
	if (!intf) {
		ast_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		err = -1;
		goto err_intf_not_found;
	}

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"vgsm_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}

	ast_mutex_lock(&intf->lock);
	if (intf->status == VGSM_INTF_STATUS_READY) {

		if (intf->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
		    intf->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
			ast_log(LOG_DEBUG, "Interface %s not registered\n",
				intf_name);
			err = -1;
			goto err_intf_not_registered;
		}

		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_INCALL,
					READY_UPDATE_TIME);

	} else if (intf->status == VGSM_INTF_STATUS_SENDING_SMS)
		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_INCALL_SENDING_SMS,
				READY_UPDATE_TIME);
	else {
		ast_log(LOG_DEBUG, "Interface %s is not ready\n", intf_name);
		err = -1;
		goto err_intf_not_ready;
	}

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	intf->current_call = ast_chan;
	vgsm_chan->intf = vgsm_intf_get(intf);

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->intf->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->intf->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->intf->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", intf->name, 1);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	struct vgsm_req *req;
	// 'timeout' instead of 20s ?
	req = vgsm_req_make_wait(
			&intf->comm, 180 * SEC,
			"ATD%c%s;",
			((ast_chan->cid.cid_pres & AST_PRES_RESTRICTION) ==
				AST_PRES_ALLOWED) ? 'i' : 'I',
			number);
	if (!req) {
		ast_log(LOG_DEBUG, "%s: Unable to dial: ATD failed\n",
			intf_name);
		err = -1;
		goto err_atd_failed;
	}

	if (req->err != VGSM_RESP_OK) {
		ast_verbose("Unable to dial: %s (%d)\n",
			vgsm_error_to_text(req->err),
			req->err);
		vgsm_req_put_null(req);

		err = -1;
		goto err_atd_failed;
	}

	vgsm_req_put_null(req);

	vgsm_connect_channel(vgsm_chan);

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);

	ast_mutex_unlock(&intf->lock);

	vgsm_intf_put(intf);

	return 0;

err_atd_failed:
err_intf_not_ready:
err_intf_not_registered:
	ast_mutex_unlock(&intf->lock);
err_channel_not_down:
	vgsm_intf_put(intf);
err_intf_not_found:
err_invalid_format:
err_invalid_destination:

	return err;
}

static int vgsm_answer(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_interface *intf = vgsm_chan->intf;
	int err;

	vgsm_debug_generic("vgsm_answer\n");

	ast_indicate(ast_chan, -1);

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return -1;
	}

	err = vgsm_req_make_wait_result(&intf->comm, 1 * SEC, "ATA");
	if (err != VGSM_RESP_OK) {
		ast_log(LOG_WARNING, "Couldn't answer: %s\n",
			vgsm_error_to_text(err));

		return -1;
	}

	vgsm_connect_channel(vgsm_chan);

	return 0;
}

static int vgsm_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc,
	int timeoutms)
{
	return AST_BRIDGE_FAILED_NOWARN;
}

struct ast_frame *vgsm_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "vgsm_exception\n");

	return NULL;
}

/* We are called with chan->lock'ed */
static int vgsm_indicate(struct ast_channel *ast_chan, int condition)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_interface *intf = vgsm_chan->intf;

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return 1;
	}

	vgsm_debug_generic("vgsm_indicate %d\n", condition);

	switch(condition) {
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
		return 1;
	break;

	case -1:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_ANSWER:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_HANGUP:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_BUSY: {
		int err = vgsm_req_make_wait_result(
				&intf->comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK)
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_error_to_text(err), err);

		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
	}
	break;
	}

	return 1;
}

static int vgsm_fixup(
	struct ast_channel *oldchan,
	struct ast_channel *newchan)
{
	struct vgsm_chan *chan = to_vgsm_chan(newchan);

	if (chan->ast_chan != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n",
				oldchan, chan->ast_chan);
		return -1;
	}

	chan->ast_chan = newchan;

	return 0;
}

static int vgsm_setoption(
	struct ast_channel *ast_chan,
	int option,
	void *data,
	int datalen)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int vgsm_transfer(
	struct ast_channel *ast,
	const char *dest)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int vgsm_send_digit(struct ast_channel *ast_chan, char digit)
{
	ast_log(LOG_NOTICE, "%s %c\n", __FUNCTION__, digit);

	return 1;
}

static int vgsm_sendtext(struct ast_channel *ast, const char *text)
{
	ast_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void vgsm_disconnect_channel(
	struct vgsm_chan *vgsm_chan)
{
	ast_mutex_lock(&vgsm_chan->ast_chan->lock);

	if (vgsm_chan->sp_fd < 0) {
		ast_mutex_unlock(&vgsm_chan->ast_chan->lock);
		return;
	}

	struct visdn_connect vc;
	vc.pipeline_id = vgsm_chan->pipeline_id;
	vc.src_chan_id = 0;
	vc.dst_chan_id = 0;
	vc.flags = 0;

	if (ioctl(vgsm.router_control_fd,
			VISDN_IOC_DISCONNECT,
			(caddr_t)&vc) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_DISCONNECT):"
			" %s\n",
			strerror(errno));
	}

	if (close(vgsm_chan->sp_fd) < 0) {
		ast_log(LOG_ERROR,
			"close(vgsm_chan->sp_fd): %s\n",
			strerror(errno));
	}

	vgsm_chan->sp_fd = -1;

	ast_mutex_unlock(&vgsm_chan->ast_chan->lock);
}

static int vgsm_hangup(struct ast_channel *ast_chan)
{
	vgsm_debug_generic("vgsm_hangup %s\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_interface *intf = vgsm_chan->intf;

	if (intf) {
		int err = vgsm_req_make_wait_result(
				&intf->comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_error_to_text(err), err);
			goto done;
		}

done:
		if (intf->status == VGSM_INTF_STATUS_INCALL)
			vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
						READY_UPDATE_TIME);
		else if (intf->status == VGSM_INTF_STATUS_INCALL_SENDING_SMS)
			vgsm_intf_set_status(intf, VGSM_INTF_STATUS_SENDING_SMS,
						READY_UPDATE_TIME);

		intf->current_call = NULL;
	}

	ast_mutex_lock(&ast_chan->lock);

	close(ast_chan->fds[0]);

	if (vgsm_chan) {
		if (vgsm_chan->sp_fd >= 0)
			vgsm_disconnect_channel(vgsm_chan);

		vgsm_destroy(vgsm_chan);
		ast_chan->tech_pvt = NULL;
	}

	if (vgsm_chan->dsp) {
		ast_dsp_free(vgsm_chan->dsp);
		vgsm_chan->dsp = NULL;
	}

	ast_setstate(ast_chan, AST_STATE_DOWN);

	ast_mutex_unlock(&ast_chan->lock);

	vgsm_debug_generic("vgsm_hangup complete\n");

	return 0;
}

static struct ast_frame *vgsm_read(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	static struct ast_frame f;
	char buf[512];

	read(ast_chan->fds[0], buf, 1);

	f.src = VGSM_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	if (vgsm_chan->sp_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;

		return &f;
	}

	int nread = read(vgsm_chan->sp_fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return &f;
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "R %.3f %02x%02x%02x%02x%02x%02x%02x%02x %d\n",
	t/1000000.0,
	*(__u8 *)(buf + 0),
	*(__u8 *)(buf + 1),
	*(__u8 *)(buf + 2),
	*(__u8 *)(buf + 3),
	*(__u8 *)(buf + 4),
	*(__u8 *)(buf + 5),
	*(__u8 *)(buf + 6),
	*(__u8 *)(buf + 7),
	nread);
#endif

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = nread;
	f.datalen = nread;
	f.data = buf;
	f.offset = 0;

	struct ast_frame *f2 = ast_dsp_process(ast_chan, vgsm_chan->dsp, &f);

	return f2;
}

static int vgsm_write(
	struct ast_channel *ast_chan,
	struct ast_frame *frame)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING,
			"Don't know what to do with frame type '%d'\n",
			frame->frametype);

		return 0;
	}

	if (frame->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (vgsm_chan->sp_fd < 0) {
//		ast_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "W %.3f %02x%02x%02x%02x%02x%02x%02x%02x %d\n",
	t/1000000.0,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);
#endif

	write(vgsm_chan->sp_fd, frame->data, frame->datalen);

	return 0;
}

static struct ast_channel *vgsm_request(
	const char *type, int format, void *data, int *cause)
{
	struct vgsm_chan *vgsm_chan;

	if (!(format & AST_FORMAT_ALAW)) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		goto err_unsupported_format;
	}

	vgsm_chan = vgsm_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_alloc;
	}

	struct ast_channel *ast_chan;
	ast_chan = vgsm_new(vgsm_chan, AST_STATE_DOWN);

	if (!ast_chan)
		goto err_vgsm_new;

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VGSM/null");

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	return ast_chan;

err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:
err_unsupported_format:

	return NULL;
}

static const struct ast_channel_tech vgsm_tech = {
	.type		= VGSM_CHAN_TYPE,
	.description	= VGSM_DESCRIPTION,
	.capabilities	= AST_FORMAT_ALAW,
	.requester	= vgsm_request,
	.call		= vgsm_call,
	.hangup		= vgsm_hangup,
	.answer		= vgsm_answer,
	.read		= vgsm_read,
	.write		= vgsm_write,
	.indicate	= vgsm_indicate,
	.transfer	= vgsm_transfer,
	.fixup		= vgsm_fixup,
	.send_digit	= vgsm_send_digit,
	.bridge		= vgsm_bridge,
	.send_text	= vgsm_sendtext,
	.setoption	= vgsm_setoption,
};

#define to_intf(cm) container_of((cm), struct vgsm_interface, comm)

static void vgsm_intf_counter_inc(
	struct vgsm_interface *intf,
	int location,
	int reason)
{
	struct vgsm_counter *counter;
	list_for_each_entry(counter, &intf->counters, node) {
		if (counter->location == location &&
		    counter->reason == reason) {
			counter++;
			goto found;
		}
	}

	counter = malloc(sizeof(*counter));
	if (!counter)
		return;

	counter->location = location;
	counter->reason = reason;
	counter->count = 1;

	list_add(&counter->node, &intf->counters);
	
found:;
}

static int vgsm_request_ceer(struct vgsm_interface *intf)
{
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(&intf->comm, 5 * SEC, "AT+CEER");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
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

	vgsm_intf_counter_inc(intf, location, reason);

	ast_log(LOG_NOTICE,
		"Call released, location '%s', cause '%s'\n",
		vgsm_cause_location_to_text(location),
		vgsm_cause_reason_to_text(location, reason));

	return vgsm_cause_to_ast_cause(location, reason);

err_cerr:
	vgsm_req_put_null(req);

	return AST_CAUSE_NORMAL_CLEARING;
}


static void vgsm_common_hangup(struct vgsm_interface *intf)
{
	int cause = vgsm_request_ceer(intf);

	ast_mutex_lock(&intf->lock);
	if (intf->current_call) {
		intf->current_call->hangupcause = cause;
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
	}
	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_no_carrier(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);

	vgsm_common_hangup(intf);
}

static void handle_unsolicited_no_dialtone(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);

	vgsm_common_hangup(intf);
}

static void handle_unsolicited_busy(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);

	vgsm_common_hangup(intf);
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
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&intf->lock);

	if (intf->status == VGSM_INTF_STATUS_RING ||
	    intf->status == VGSM_INTF_STATUS_INCALL) {
		ast_mutex_unlock(&intf->lock);
		return;
	}

	if (intf->status != VGSM_INTF_STATUS_READY) {
		ast_log(LOG_NOTICE,
			"Rejecting RING on not ready interface\n");
		int err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_error_to_text(err), err);
			// FIXME TODO
		}

		goto err_intf_not_ready;
	}

	if (strcmp(pars, "VOICE")) {
		ast_log(LOG_NOTICE, "Not a voice call, rejecting\n");

		int err = vgsm_req_make_wait_result(comm, 5 * SEC, "AT+CHUP");
		if (err != VGSM_RESP_OK) {
			// FIXME TODO
			ast_log(LOG_ERROR,
				"Error hanging up: %s (%d)\n",
				vgsm_error_to_text(err), err);
		}

		goto err_not_voice;
	}

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_RING, -1);

	ast_mutex_unlock(&intf->lock);

	return;

err_not_voice:
err_intf_not_ready:

	ast_mutex_unlock(&intf->lock);

	return;
}

static void vgsm_update_intf_by_creg(
	struct vgsm_interface *intf,
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
	case 0: intf->net.status = VGSM_NET_STATUS_NOT_SEARCHING; break;
	case 1: intf->net.status = VGSM_NET_STATUS_REGISTERED_HOME; break;
	case 2: intf->net.status = VGSM_NET_STATUS_NOT_REGISTERED; break;
	case 3: intf->net.status = VGSM_NET_STATUS_REGISTRATION_DENIED; break;
	case 4: intf->net.status = VGSM_NET_STATUS_UNKNOWN; break;
	case 5: intf->net.status = VGSM_NET_STATUS_REGISTERED_ROAMING; break;
	}

	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		// Update Net Info FIXME TODO
	}

	return;

err_no_status:
err_no_mode:

	return;
}

static int vgsm_update_cops(
	struct vgsm_interface *intf)
{
	int err;

	struct vgsm_req *req;

	req = vgsm_req_make_wait(&intf->comm, 180 * SEC, "AT+COPS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
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
	if (!get_token(&line, intf->net.operator_id,
			sizeof(intf->net.operator_id)))
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
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&intf->lock);

	vgsm_update_intf_by_creg(intf, pars, FALSE);

	vgsm_debug_generic(
		"Module '%s' registration %s\n",
		intf->name,
		vgsm_net_status_to_text(intf->net.status));

	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING)
		vgsm_update_cops(intf);

	ast_mutex_unlock(&intf->lock);
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
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);
	const char *pars_ptr = pars;
	char field[32];

	ast_mutex_lock(&intf->lock);
	if (intf->status != VGSM_INTF_STATUS_RING)
		goto err_not_incall;

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_INCALL,
				READY_UPDATE_TIME);

	intf->current_call = vgsm_alloc_inbound_call(intf);
	if (!intf->current_call) {
		vgsm_req_put(vgsm_req_make(&intf->comm, 5 * SEC, "AT+CHUP"));

		goto err_alloc_call;
	}

	intf->current_call->cid.cid_pres =
		AST_PRES_USER_NUMBER_UNSCREENED |
		AST_PRES_UNAVAILABLE;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_WARNING, "Cannot parse CID '%s'\n", pars);
		goto err_parse_cid;
	}

	intf->current_call->cid.cid_num = strdup(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	intf->current_call->cid.cid_ton = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	intf->current_call->cid.cid_name = strdup(field);

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	switch(atoi(field)) {
	case 0:
		intf->current_call->cid.cid_pres =
			AST_PRES_USER_NUMBER_PASSED_SCREEN |
			AST_PRES_ALLOWED;
	break;

	case 1:
		intf->current_call->cid.cid_pres =
			AST_PRES_USER_NUMBER_PASSED_SCREEN |
			AST_PRES_RESTRICTED;
	break;

	case 2:
		intf->current_call->cid.cid_pres =
			AST_PRES_USER_NUMBER_UNSCREENED |
			AST_PRES_UNAVAILABLE;
	break;
	}

parsing_done:

	if (ast_pbx_start(intf->current_call)) {
		ast_log(LOG_ERROR,
			"Unable to start PBX on %s\n",
			intf->current_call->name);
		goto err_start_pbx;
	}

	ast_mutex_unlock(&intf->lock);

	return;

err_start_pbx:
err_parse_cid:
	ast_hangup(intf->current_call);
	intf->current_call = NULL;
err_alloc_call:
err_not_incall:
	ast_mutex_unlock(&intf->lock);

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
	struct vgsm_interface *intf = to_intf(comm);
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

	sms->intf = intf;

	if (vgsm_sms_spool(sms) >= 0) {

		/* Send Acknowledgment */

		int err = vgsm_req_make_wait_result(comm,
				20 * SEC, "AT+CNMA=0");
		if (err != VGSM_RESP_OK) {
			ast_log(LOG_ERROR,
				"Error acknowledging SMS: %s (%d)\n",
				vgsm_error_to_text(err), err);
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
			vgsm_error_to_text(err), err);
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

static void vgsm_handle_clcc(
	struct vgsm_interface *intf,
	int id,
	int direction,
	int state,
	int mode)
{
	switch (state) {
	case 0: /* Active */
		if (!intf->current_call) {
			ast_log(LOG_ERROR,
				"Call is active but there is no"
				" current call\n");
			vgsm_req_put(vgsm_req_make(&intf->comm,
					5 * SEC, "AT+CHUP"));

			return;
		}

		if (intf->current_call->_state != AST_STATE_UP) {
			ast_setstate(intf->current_call, AST_STATE_UP);
			ast_queue_control(intf->current_call,
						AST_CONTROL_ANSWER);
		}
	break;

	case 1: /* Held */
		ast_log(LOG_DEBUG, "Unsupported state 1-held\n");
	break;

	case 2: /* Dialing */
		/* Do nutting */
	break;

	case 3: /* Alerting */
		if (!intf->current_call) {
			ast_log(LOG_ERROR,
				"Call is alerting but there is no"
				" current call\n");

			vgsm_req_put(vgsm_req_make(&intf->comm,
						5 * SEC, "AT+CHUP"));

			return;
		}

		if (intf->current_call->_state != AST_STATE_RINGING) {
			ast_setstate(intf->current_call, AST_STATE_RINGING);
			ast_queue_control(intf->current_call,
						AST_CONTROL_RINGING);
		}
	break;

	case 4: /* Incoming */
	break;

	case 5: /* Waiting */
		ast_log(LOG_ERROR, "Unsupported state 5-waiting\n");
	break;

	case 6: /* Terminating */
	break;

	case 7: /* Dropped */
		ast_queue_control(intf->current_call, AST_CONTROL_DISCONNECT);
	break;
	}
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
	struct vgsm_interface *intf = to_intf(comm);
	int err;
	int call_present = FALSE;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SLCC");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		return;
	}

	ast_mutex_lock(&intf->lock);

	struct vgsm_req_line *line;
	list_for_each_entry(line, &req->lines, node) {
		const char *pars = line->text + strlen("^SLCC: ");

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

		int id = atoi(field);

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' direction\n",
				line->text);
			continue;
		}

		int direction = atoi(field);

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse RLCC '%s' state\n",
				line->text);
			continue;
		}

		int state = atoi(field);

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' mode\n",
				line->text);
			continue;
		}

		int mode = atoi(field);

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' mpty\n",
				line->text);
			continue;
		}

//		int multiparty = atoi(field);

		if (!get_token(&pars_ptr, field, sizeof(field))) {
			ast_log(LOG_ERROR, "Cannot parse SLCC '%s' number\n",
				line->text);
			continue;
		}

		if (id != 1) {
			ast_log(LOG_WARNING,
				"Don't know how to handle call id '%d'\n",
				id);
			continue;
		}

		call_present = TRUE;

		vgsm_handle_clcc(intf, id, direction, state, mode);
	}

	vgsm_req_put_null(req);

	ast_mutex_unlock(&intf->lock);

	if (!call_present) {
		if(intf->current_call && intf->current_call->pbx) {
			ast_log(LOG_NOTICE,
				"SLCC has no call, requesting HANGUP\n");
			vgsm_common_hangup(intf);
		}
        }
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
	struct vgsm_interface *intf,
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

static int vgsm_update_smond(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	int err;

	ast_mutex_lock(&intf->lock);

	intf->net.ncells = 0;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SMOND");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
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

	if (vgsm_module_update_common_cell_info(intf, &intf->net.sci,
			       				&pars_ptr) < 0)
		goto err_moni;

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevFull '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.rx_lev_full);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxLevSub '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.rx_lev_sub);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQual '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.rx_qual_full);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualFull '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.rx_qual_sub);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RxQualSub '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.timeslot);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse Timeslot '%s'\n", line);
		goto err_moni;
	}

	int i;
	for (i=0; i<6; i++) {
		if (vgsm_module_update_common_cell_info(intf,
				&intf->net.nci[intf->net.ncells],
				&pars_ptr) < 0)
			goto err_moni;

		if (intf->net.nci[intf->net.ncells].mnc != 0)
			intf->net.ncells++;
	}

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse TA '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.ta);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse RSSI '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.rssi);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse BER '%s'\n", line);
		goto err_moni;
	}

	sscanf(field, "%d", &intf->net.sci2.ber);

	vgsm_req_put_null(req);

	ast_mutex_unlock(&intf->lock);

	return 0;
	
err_moni:

	ast_mutex_unlock(&intf->lock);

	return -1;
}

static void handle_unsolicited_ciev_rssi(
	const struct vgsm_req *urc,
	const char *pars)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);
	
	vgsm_update_smond(intf);
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
	struct vgsm_interface *intf = to_intf(comm);

	vgsm_debug_generic("Module started (^SYSSTART received)\n");

	vgsm_intf_set_status(intf,
		VGSM_INTF_STATUS_WAITING_INITIALIZATION,
		WAITING_INITIALIZATION_DELAY);
}

static void handle_unsolicited_shutdown(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);

	vgsm_debug_generic("Module powered off (^SHUTDOWN received)\n");

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_OFF, -1);
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
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_mutex_lock(&intf->lock);
	if (atoi(pars)) {
		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_WAITING_INITIALIZATION,
			WAITING_INITIALIZATION_DELAY);
		intf->sim.inserted = TRUE;
	} else {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_SIM, -1);
		intf->sim.inserted = FALSE;
	}
	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_sbc(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	ast_log(LOG_ERROR,
		"%s: Power supply: %s\n", pars, intf->name);
}

static void handle_unsolicited_sstn(
	const struct vgsm_req *urc)
{
}

static void handle_unsolicited_sctm_a(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Battery is under critical low temperature limit"
			" and shutting down\n", intf->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Battery is under lower temperature limit\n",
			intf->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Battery temperature is now ok\n",
			intf->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Battery is over high temperature limit\n",
			intf->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Battery is over critical high temperature limit"
			" and shutting down\n", intf->name);
	break;
	}
}

static void handle_unsolicited_sctm_b(
	const struct vgsm_req *urc)
{
	struct vgsm_comm *comm = urc->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_req_first_line(urc)->text;
	const char *pars = line + strlen(urc->urc_class->code);

	switch(atoi(pars)) {
	case -2:
		ast_log(LOG_ERROR,
			"%s: Engine is under critical low temperature limit"
			" and shutting down\n", intf->name);
	break;

	case -1:
		ast_log(LOG_WARNING,
			"%s: Engine is under lower temperature limit\n",
			intf->name);
	break;

	case 0:
		ast_log(LOG_NOTICE,
			"%s: Engine temperature is now ok\n",
			intf->name);
	break;

	case 1:
		ast_log(LOG_WARNING,
			"%s: Engine is over high temperature limit\n",
			intf->name);
	break;

	case 2:
		ast_log(LOG_ERROR,
			"%s: Engine is over critical high temperature limit"
			" and shutting down\n", intf->name);
	break;
	}
}

static struct vgsm_urc_class urc_classes[] =
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

static int vgsm_pin_check_and_input(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_req *req;
	const struct vgsm_req_line *first_line;
	int err;
	int res = 0;

	/* Be careful to not consume all the available attempts */
	req = vgsm_req_make_wait(comm, 10 * SEC, "AT^SPIC");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_SIM, -1);
		vgsm_intf_setreason(intf, "SIM not present");
		vgsm_req_put_null(req);
		goto err_spic;
	} else if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_spic;
	}

	intf->sim.remaining_attempts =
		atoi(vgsm_req_first_line(req)->text + strlen("^SPIC: "));

	vgsm_req_put_null(req);

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err == CME_ERROR(10)) {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_SIM, -1);
		vgsm_intf_setreason(intf, "SIM not present");
		vgsm_req_put_null(req);
		goto err_spic;
	} else if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_spic;
	}

	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		/* Do nothing */
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		if (intf->sim.remaining_attempts < 3) {
			vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_WAITING_PIN, -1);
			vgsm_intf_setreason(intf, "Input PIN manually");
			res = -1;
		} else if (strlen(intf->pin)) {
			int err = vgsm_req_make_wait_result(comm, 20 * SEC,
					"AT+CPIN=\"%s\"", intf->pin);

			if (err != VGSM_RESP_OK) {
				vgsm_intf_set_status(intf,
					VGSM_INTF_STATUS_WAITING_PIN, -1);
				vgsm_intf_setreason(intf,
					"SIM PIN refused (%s), input manually",
					vgsm_error_to_text(err));
				res = -1;
			}
		} else {
			vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_WAITING_PIN, -1);
			vgsm_intf_setreason(intf,
				"SIM PIN not configured, input manually");
			res = -1;
		}
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_PIN, -1);
		vgsm_intf_setreason(intf, "SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_PIN, -1);
		vgsm_intf_setreason(intf, "SIM requires PUK, input manually");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_WAITING_PIN, -1);
		vgsm_intf_setreason(intf, "SIM requires PUK2");
		res = -1;
	} else {
		vgsm_comm_disable(&intf->comm);
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
		vgsm_intf_setreason(intf,
			"Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_req_put_null(req);

	return res;

err_spic:

	return -1;
}

static int vgsm_module_codec_init(struct vgsm_interface *intf)
{
	struct vgsm_codec_ctl cctl;

	cctl.parameter = VGSM_CODEC_RXGAIN;
	cctl.value = intf->rx_gain;

	sleep(1);
	if (ioctl(intf->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(IOC_CODEC_SET, RXGAIN) failed: %s\n",
			strerror(errno));

		return -1;
	}

	cctl.parameter = VGSM_CODEC_TXGAIN;
	cctl.value = intf->tx_gain;

	sleep(1);
	if (ioctl(intf->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(IOC_CODEC_SET, TXGAIN) failed: %s\n",
			strerror(errno));

		return -1;
	}

	sleep(1);
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
}

static int vgsm_module_prepin_configure(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
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
	if (intf->set_clock) {
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
			vgsm_intf_unexpected_error(intf, err);
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

	vgsm_intf_unexpected_error(intf, err);

	return -1;
}

static int vgsm_module_configure(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	int err;

	/* Configure operator selection */
	switch(intf->operator_selection) {
	case VGSM_OPSEL_AUTOMATIC:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=0,2");
	break;

	case VGSM_OPSEL_MANUAL_UNLOCKED:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=1,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_FALLBACK:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=4,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_LOCKED:
		err = vgsm_req_make_wait_result(
			comm, 180 * SEC, "AT+COPS=5,2,%s",
			intf->operator_id);
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
		vgsm_intf_unexpected_error(intf, err);

	/* Select message service  */
	err = vgsm_req_make_wait_result(comm, 100 * MILLISEC, "AT+CSMS=1");
	if (err != VGSM_RESP_OK)
		vgsm_intf_unexpected_error(intf, err);

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

	vgsm_intf_unexpected_error(intf, err);

	return -1;
}

static int vgsm_module_update_static_info(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_req *req;
	int err;

	ast_mutex_lock(&intf->lock);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(intf->module.vendor,
		vgsm_req_first_line(req)->text,
		sizeof(intf->module.vendor));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMM");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(intf->module.model,
		vgsm_req_first_line(req)->text,
		sizeof(intf->module.model));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CGMR");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(intf->module.version,
		vgsm_req_first_line(req)->text,
		sizeof(intf->module.version));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CGSN");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(intf->module.imei,
		vgsm_req_first_line(req)->text,
		sizeof(intf->module.imei));

	vgsm_req_put_null(req);

	
	/*--------*/
	req = vgsm_req_make_wait(comm, 100 * MILLISEC, "AT^SCKS?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
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
			intf->sim.inserted = TRUE;
		else
			intf->sim.inserted = FALSE;
	}

	vgsm_req_put_null(req);

	if (!intf->sim.inserted)
		goto no_sim;
	
	/*--------*/
	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CIMI");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	strncpy(intf->sim.imsi,
		vgsm_req_first_line(req)->text,
		sizeof(intf->sim.imsi));

	vgsm_req_put_null(req);

	/*--------*/
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CXXCID");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_failure;
	}

	if (strlen(vgsm_req_first_line(req)->text) < strlen("+CXXCID: "))
		goto err_failure;

	strncpy(intf->sim.card_id,
		vgsm_req_first_line(req)->text + strlen("+CXXCID: "),
		sizeof(intf->sim.card_id));

	vgsm_req_put_null(req);

no_sim:

	ast_mutex_unlock(&intf->lock);

	return 0;

err_failure:

	ast_mutex_unlock(&intf->lock);

	return -1;
}

static int vgsm_module_update_net_info(
	struct vgsm_interface *intf)
{
	int err;
	struct vgsm_comm *comm = &intf->comm;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 5 * SEC, "AT+CREG?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_intf_unexpected_error(intf, err);
		vgsm_req_put_null(req);
		goto err_creg_read_response;
	}

	const char *line = vgsm_req_first_line(req)->text;

	if (strlen(line) > strlen("+CREG: "))
		vgsm_update_intf_by_creg(intf, line + strlen("+CREG: "), TRUE);

	vgsm_req_put_null(req);

	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {

		err = vgsm_update_smond(intf);
		if (err < 0)
			goto err_update_smond;

		err = vgsm_update_cops(intf);
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

/*! \brief  manager_vgsm_sms_tx: Send a text sms with VGSM card ---*/

static int manager_vgsm_sms_tx(struct mansession *s, struct message *m)
{
	char *interface = astman_get_header(m, "Interface");
	char *number = astman_get_header(m, "Destination");
	char *class_str = astman_get_header(m, "Class");
	char *content = astman_get_header(m, "Content");
	char *hex_content = astman_get_header(m, "HexContent");

	if (!strlen(number)) {
		astman_send_error(s, m, "Destination missing");
		goto err_missing_destination;
	}

	if (!strlen(content) && !strlen(hex_content)) {
		astman_send_error(s, m, "Content missing");
		goto err_missing_content;
	}

	/* start interface guessing */
	struct vgsm_interface *intf;
	int found = 0;

	/* if user has specified an interface, let's find it */
	if(strlen(interface)) {
		intf = vgsm_intf_get_by_name(interface);
		if (!intf) {
			astman_send_error(s, m, "Cannot find interface");
			goto err_intf_not_found;
		}
	} else {
		/* if user has not specified an interface, get the first available */
		ast_mutex_lock(&vgsm.lock);
		list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
			        ast_mutex_lock(&intf->lock);
			        if (intf->status == VGSM_INTF_STATUS_READY ||
			            intf->status == VGSM_INTF_STATUS_INCALL) {
	                		ast_mutex_unlock(&intf->lock);
					found = 1;
					break;
	        		}
	        		ast_mutex_unlock(&intf->lock);
		}
		ast_mutex_unlock(&vgsm.lock);
		if (!found) {
			astman_send_error(s, m, "Cannot find any interface");
			goto err_intf_not_found;
		}
	}

	/* check again that the intf is available */
	ast_mutex_lock(&intf->lock);
	if (intf->status == VGSM_INTF_STATUS_READY) {
		if (intf->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
		    intf->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
			ast_log(LOG_DEBUG, "Interface %s not registered\n",
				intf->name);
			goto err_intf_not_registered;
		}

		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_SENDING_SMS, -1);

	} else if (intf->status == VGSM_INTF_STATUS_INCALL)
		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_INCALL_SENDING_SMS,
				READY_UPDATE_TIME);
	else {
		astman_send_error(s, m,
			"Interface is busy");
		ast_mutex_unlock(&intf->lock);
		goto err_intf_not_ready;
	}
	ast_mutex_unlock(&intf->lock);

	/* now the intf is ok, starting sms setup */
	struct vgsm_sms *sms;
	sms = vgsm_sms_alloc();
	if (!sms) {
		astman_send_error(s, m,
			"Cannot allocate SMS");
		goto err_sms_alloc;
	}

	sms->intf = intf;
	vgsm_number_parse(
		intf->sms_service_center,
		sms->smcc, sizeof(sms->smcc),
		&sms->smcc_np, &sms->smcc_ton);

	vgsm_number_parse(
		number,
		sms->dest, sizeof(sms->dest),
		&sms->dest_np,
		&sms->dest_ton);

	sms->timestamp = time(NULL);

	tzset();
	sms->timezone = timezone;

	if (strlen(class_str))
		sms->message_class = atoi(class_str);
	else
		sms->message_class = 1;

	size_t content_size;
	char *content_raw;

	if (strlen(hex_content)) {
		content_size = strlen(hex_content) / 2;
		content_raw = alloca(content_size + 1);
		if (!content_raw)
			goto err_alloc_content;

		int i;
		for(i=0; i<content_size; i++) {
			content_raw[i] =
				char_to_hexdigit(hex_content[i * 2]) << 4 |
				char_to_hexdigit(hex_content[i * 2 + 1]);
		}

		content_raw[content_size] = '\0';
	} else {
		content_size = strlen(content);
		content_raw = content;
	}

	iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
	if (cd < 0) {
		ast_log(LOG_ERROR, "Cannot open iconv context; %s\n",
			strerror(errno));
		goto err_iconv_open;
	}

	char *inbuf = (char *)content_raw;
	size_t inbytes = content_size;
	size_t outbytes = 1024;

	inbuf = (char *)content_raw;
	inbytes = content_size;

	sms->text = malloc(outbytes + sizeof(wchar_t));
	if (!sms->text) {
		iconv_close(cd);
		goto err_text_malloc;
	}

	char *outbuf = (char *)sms->text;
	if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes) < 0) {
		iconv_close(cd);
		goto err_iconv;
	}

	iconv_close(cd);

	outbuf[outbytes] = L'\0';

	vgsm_sms_prepare(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&intf->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	if (!req) {
		ast_log(LOG_NOTICE,"vgsm: Error requesting SMS\n");
		astman_send_error(s, m, "Error sending SMS");
		goto err_req_make;
	}

	vgsm_req_wait(req);

	if (req->err != VGSM_RESP_OK) {
		ast_log(LOG_NOTICE,"SMStx: Request Response is not OK\n");
		astman_send_error(s, m, "Error sending SMS");
		goto err_req_result;
	}

	vgsm_req_put_null(req);

	astman_send_ack(s, m, "VGSMsmstx: SMS sent");

       if (intf->status == VGSM_INTF_STATUS_SENDING_SMS)
               vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
					READY_UPDATE_TIME);
	else if (intf->status == VGSM_INTF_STATUS_INCALL_SENDING_SMS)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_INCALL,
					READY_UPDATE_TIME);

	return 0;

err_req_result:
	vgsm_req_put_null(req);
err_req_make:
       if (intf->status == VGSM_INTF_STATUS_SENDING_SMS)
               vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
					READY_UPDATE_TIME);
	else if (intf->status == VGSM_INTF_STATUS_INCALL_SENDING_SMS)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_INCALL,
					READY_UPDATE_TIME);
err_iconv:
	free(sms->text);
	sms->text = NULL;
err_text_malloc:
err_iconv_open:
err_alloc_content:
	vgsm_sms_put_null(sms);
err_sms_alloc:
err_intf_not_ready:
err_intf_not_registered:
err_intf_not_found:
err_missing_content:
err_missing_destination:

	return 0;
}

/***********************************************/

static int vgsm_module_init_at_interface(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
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

	vgsm_intf_unexpected_error(intf, err);

	return -1;
}

static void vgsm_module_open(
	struct vgsm_interface *intf)
{
	if (intf->comm.fd >= 0) {
		ast_mutex_lock(&intf->lock);
		close(intf->comm.fd);
		intf->comm.fd = -1;
		ast_mutex_unlock(&intf->lock);
		vgsm_comm_wakeup(&intf->comm);
		sleep(1); // Leave time to poll to release fd
	}

	intf->comm.fd = open(intf->device_filename, O_RDWR);
	if (intf->comm.fd < 0) {
		ast_log(LOG_WARNING,
			"Unable to open '%s': %s\n",
			intf->device_filename,
			strerror(errno));

		vgsm_comm_disable(&intf->comm);
		vgsm_intf_setreason(intf, "Error opening device");
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
		return;
	}

	intf->comm.name = intf->name;

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
	
	tcflush(intf->comm.fd, TCIFLUSH);
	tcsetattr(intf->comm.fd, TCSANOW, &newtio);

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
	
	tcflush(intf->comm.fd, TCIFLUSH);
	tcsetattr(intf->comm.fd, TCSANOW, &newtio);

	/***************/

	vgsm_comm_enable(&intf->comm);

	int val;
	if (ioctl(intf->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
		fprintf(stderr, "ioctl(IOC_POWER_GET) failed: %s\n",
			strerror(errno));

		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_FAILED, 1 * SEC);

		return;
	}

	if (val) {
		vgsm_debug_generic(
			"Module is already powered on, I'm not waiting"
			" for SYSSTART\n");

		vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_WAITING_INITIALIZATION,
				0);
	} else {
		vgsm_module_ignite(intf);

		/* The very first module power-up will not send ^SYSSTART URC
		 * because it is configured in auto-bauding mode. We will time
		 * out and initialize it anyway.
		 */
		intf->power_attempts = 0;
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT);
	}
}

static void vgsm_module_initialize(
	struct vgsm_interface *intf)
{
	vgsm_debug_generic("Initializing module '%s'\n", intf->name);

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_INITIALIZING, -1);

	if (vgsm_module_codec_init(intf) < 0) {
		vgsm_comm_disable(&intf->comm);
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
		vgsm_intf_setreason(intf, "Error configuring CODEC");

		goto initialization_failure;
	}

	int val;
	if (ioctl(intf->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
		fprintf(stderr, "ioctl(IOC_POWER_GET) failed: %s\n",
			strerror(errno));

		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_FAILED, 1 * SEC);

		goto initialization_failure;
	}

	if (!val) {
		ast_log(LOG_NOTICE,
			"Module '%s' is not powered on, re-igniting\n",
			intf->name);
		
		intf->power_attempts = 0;
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_POWERING_ON,
						POWERING_ON_TIMEOUT);

		vgsm_module_ignite(intf);

		goto initialization_failure;
	}

	if (vgsm_module_init_at_interface(intf) < 0)
		goto initialization_failure;

	if (vgsm_module_prepin_configure(intf) < 0)
		goto initialization_failure;

	if (vgsm_pin_check_and_input(intf) < 0)
		goto initialization_failure;

	if (vgsm_module_configure(intf) < 0)
		goto initialization_failure;

	if (vgsm_module_update_static_info(intf) < 0)
		goto initialization_failure;

	if (vgsm_module_update_net_info(intf) < 0)
		goto initialization_failure;

	vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY, READY_UPDATE_TIME);

	vgsm_debug_generic("Module '%s' successfully initialized\n",
		intf->name);

	return;

initialization_failure:

	/* If no-one changed the status to something significant fall back to
	 * FAILED
	 */

	if (intf->status == VGSM_INTF_STATUS_INITIALIZING)
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_FAILED,
					FAILED_RETRY_TIME);
}

static void vgsm_module_monitor_timer(
	struct vgsm_interface *intf)
{
	switch(intf->status) {
	case VGSM_INTF_STATUS_CLOSED:
		vgsm_module_open(intf);
	break;

	case VGSM_INTF_STATUS_POWERING_ON: {
		int val;
		if (ioctl(intf->comm.fd, VGSM_IOC_POWER_GET, &val) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_POWER_GET) failed: %s\n",
				intf->name,
				strerror(errno));

			vgsm_intf_set_status(intf,
				VGSM_INTF_STATUS_FAILED, 1 * SEC);

			return;
		}

		if (val) {
			vgsm_debug_generic("Module '%s': SYSTART missed\n",
				intf->name);

			vgsm_intf_set_status(intf,
					VGSM_INTF_STATUS_WAITING_INITIALIZATION,
					0);
		} else {
			intf->power_attempts++;
			if (intf->power_attempts > 3) {
				ast_log(LOG_ERROR,
					"%s: Power-on permanently failed\n",
					intf->name);

				vgsm_intf_set_status(intf,
					VGSM_INTF_STATUS_OFF, -1);
				vgsm_intf_setreason(intf,
					"Power-on sequence failure");
			} else {
				ast_log(LOG_NOTICE,
					"%s: Power-on sequence failed,"
					" retrying\n",
					intf->name);

				vgsm_intf_set_status(intf,
					VGSM_INTF_STATUS_POWERING_ON,
					POWERING_ON_TIMEOUT);

				vgsm_module_ignite(intf);
			}
		}
	}
	break;

	case VGSM_INTF_STATUS_POWERING_OFF:
		vgsm_module_emerg_off(intf);

		vgsm_intf_set_status(intf,
			VGSM_INTF_STATUS_OFF, -1);
	break;

	case VGSM_INTF_STATUS_FAILED:
	case VGSM_INTF_STATUS_WAITING_INITIALIZATION:
		vgsm_module_initialize(intf);
	break;

	case VGSM_INTF_STATUS_READY:
		vgsm_module_update_net_info(intf);

		/* Re-arm timer */
		vgsm_intf_set_status(intf, VGSM_INTF_STATUS_READY,
						READY_UPDATE_TIME);
	break;

	case VGSM_INTF_STATUS_INITIALIZING:
	case VGSM_INTF_STATUS_SENDING_SMS:
	case VGSM_INTF_STATUS_OFF:
		ast_log(LOG_ERROR,
			"vgsm: Module '%s': Unexpected timer in status %s\n",
			intf->name,
			vgsm_intf_status_to_text(intf->status));
	break;

	case VGSM_INTF_STATUS_RING:
	case VGSM_INTF_STATUS_INCALL:
	case VGSM_INTF_STATUS_INCALL_SENDING_SMS:
//		vgsm_call_monitor(intf);
	break;

	case VGSM_INTF_STATUS_WAITING_SIM:
	case VGSM_INTF_STATUS_WAITING_PIN:
		// Do nothing
	break;
	}
}

static void *vgsm_module_monitor_thread_main(void *data)
{
	struct vgsm_interface *intf = (struct vgsm_interface *)data;

	for(;;) {
		if (intf->timer_expiration != -1) {
			longtime_t timeout = intf->timer_expiration -
						longtime_now();
			if (timeout < 0) {
				intf->timer_expiration = -1;
				vgsm_module_monitor_timer(intf);
			} else if (timeout > 100000000) {
				sleep(timeout / 1000000);
			} else {
				usleep(timeout);
			}
		} else {
			sleep(3600);
		}
	}

	return NULL;
}

static char mandescr_vgsm_sms_tx[] =
"Description: Send and SMS in text format with VGSM channel driver.\n"
"Variables: \n"
"  Content: <text>      Text of the message, max 160 chars (will be cut if longer).\n"
"  ActionID: <id>       Action ID for this transaction. Will be returned.\n";

static void vgsm_shutdown_modules(void)
{
	struct vgsm_interface *intf;

	ast_verbose("vgsm: powering off all modules\n");

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		if (intf->poweroff_on_exit) {
			vgsm_intf_set_status(intf,
					VGSM_INTF_STATUS_POWERING_OFF,
					POWERING_OFF_TIMEOUT);

			vgsm_req_put(vgsm_req_make(&intf->comm,
						3 * SEC, "AT^SMSO"));
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	int i;
	for(i=0; i<10; i++) {
		int not_off = FALSE;

		list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

			if (intf->poweroff_on_exit &&
			    intf->status != VGSM_INTF_STATUS_OFF)
				not_off = TRUE;
		}

		if (!not_off)
			return;

		sleep(1);
	}

	ast_verbose("vgsm: Failure to power off all modules\n");
}
	

int load_module()
{
	int err;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	ast_mutex_init(&vgsm.lock);

	ast_mutex_init(&vgsm.usecnt_lock);

	INIT_LIST_HEAD(&vgsm.ifs);
	INIT_LIST_HEAD(&vgsm.op_list);

	vgsm.router_control_fd = open("/dev/visdn/router-control", O_RDWR);
	if (vgsm.router_control_fd < 0) {
		ast_log(LOG_ERROR, "Unable to open router-control: %s\n",
			strerror(errno));
		err = -1;
		goto err_open_router_control;
	}

	vgsm_reload_config();

	if (ast_channel_register(&vgsm_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VGSM_CHAN_TYPE);
		err = -1;
		goto err_channel_register;
	}

	ast_cli_register(&debug_vgsm_generic);
	ast_cli_register(&no_debug_vgsm_generic);
	ast_cli_register(&debug_vgsm_serial);
	ast_cli_register(&no_debug_vgsm_serial);
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&show_vgsm_interfaces);
	ast_cli_register(&vgsm_send_sms);
	ast_cli_register(&vgsm_power);
	ast_cli_register(&vgsm_pin_input);
	ast_cli_register(&vgsm_puk_input);
	ast_cli_register(&vgsm_pin_set);

	if (vgsm_comm_thread_create() < 0) {
		ast_log(LOG_ERROR, "Unable to start communication thread.\n");
		err = -1;
		goto err_comm_thread_create;
	}
	
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		ast_pthread_create(&intf->monitor_thread, &attr,
			vgsm_module_monitor_thread_main, intf);
	}

	/* Register manager commands */
	ast_manager_register2("VGSMsmstx", EVENT_FLAG_SYSTEM,
			manager_vgsm_sms_tx,
			"Send sms with VGSM (text format)",
			mandescr_vgsm_sms_tx);

	ast_register_atexit(vgsm_shutdown_modules);

	return 0;

	// Kill comm thread
err_comm_thread_create:
	ast_channel_unregister(&vgsm_tech);
err_channel_register:
	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_power);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_serial);
	ast_cli_unregister(&debug_vgsm_serial);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	close(vgsm.router_control_fd);
err_open_router_control:

	return err;
}

int unload_module(void)
{
	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_power);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_serial);
	ast_cli_unregister(&debug_vgsm_serial);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	close(vgsm.router_control_fd);

	return 0;
}

int reload(void)
{
	vgsm_reload_config();

	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&vgsm.usecnt_lock);
	res = vgsm.usecnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	return res;
}

char *description()
{
	return VGSM_DESCRIPTION;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

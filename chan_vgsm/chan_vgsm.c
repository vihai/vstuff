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

#include <linux/vgsm.h>

#include <linux/visdn/streamport.h>
#include <linux/visdn/router.h>

#include "util.h"
#include "chan_vgsm.h"
#include "comm.h"

#define VGSM_DESCRIPTION "VoiSmart VGSM Channel For Asterisk"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"
#define VGSM_OP_CONFIG_FILE "vgsm_operators.conf"

struct vgsm_state vgsm = {
	.usecnt = 0,
#ifdef DEBUG_DEFAULTS
	.debug = TRUE,
#else
	.debug = FALSE,
#endif

	.default_intf = {
		.context = "vgsm",
		.pin = "",
		.rx_gain = 255,
		.tx_gain = 255,
		.set_clock = 0,
		.operator_selection = VGSM_OPSEL_AUTOMATIC,
		.operator_id = "",
	}
};

static struct sched_context *sched;

static const char *vgsm_intf_status_to_text(enum vgsm_intf_status status)
{
	switch(status) {
	case VGSM_INTF_STATUS_UNINITIALIZED:
		return "UNINITIALIZED";
	case VGSM_INTF_STATUS_WAITING_INITIALIZATION:
		return "WAITING_INITIALIZATION";
	case VGSM_INTF_STATUS_INITIALIZING:
		return "INITIALIZING";
	case VGSM_INTF_STATUS_READY:
		return "READY";
	case VGSM_INTF_STATUS_INCALL:
		return "INCALL";
	case VGSM_INTF_STATUS_NO_NET:
		return "NO_NET";
	case VGSM_INTF_STATUS_WAITING_PIN:
		return "WAITING_PIN";
	case VGSM_INTF_STATUS_LOCKED_DOWN:
		return "LOCKED_DOWN";
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

struct vgsm_error_code
{
	int code;
	const char *text;
};

struct vgsm_error_code vgsm_error_codes[] =
{
	{ 0, "phone failure" },
	{ 1, "no connection to phone" },
	{ 2, "phone-adaptor link reserved" },
	{ 3, "operation not allowed" },
	{ 4, "operation not supported" },
	{ 5, "PH-SIM PIN required" },
	{ 6, "NOT SUPPORTED" },
	{ 7, "NOT SUPPORTED" },
	{ 10, "SIM not inserted" },
	{ 11, "SIM PIN required" },
	{ 12, "SIM PUK required" },
	{ 13, "SIM failure" },
	{ 14, "SIM busy" },
	{ 15, "SIM wrong" },
	{ 16, "incorrect password" },
	{ 17, "SIM PIN2 required" },
	{ 18, "SIM PUK2 required" },
	{ 20, "memory full" },
	{ 21, "invalid index" },
	{ 22, "not found" },
	{ 23, "memory failure" },
	{ 24, "text string too long" },
	{ 25, "invalid characters in text string" },
	{ 26, "dial string too long" },
	{ 27, "invalid characters in dial string" },
	{ 30, "no network service" },
	{ 31, "network timeout" },
	{ 32, "network not allowed - emergency calls only" },
	{ 40, "network personalization PIN required" },
	{ 41, "network personalization PUK required" },
	{ 42, "network subset personalization PIN required" },
	{ 43, "network subset personalization PUK required" },
	{ 44, "service provider personalization PIN required" },
	{ 45, "service provider personalization PUK required" },
	{ 46, "corporate personalization PIN required" },
	{ 47, "corporate personalization PUK required" },
	{ 100, "unknown" },
	{ 103, "illegal MS" },
	{ 106, "illegal ME" },
	{ 107, "GPRS service not allowed" },
	{ 111, "PLMN not allowed" },
	{ 112, "location area not allowed" },
	{ 113, "roaming not allowed in this location area" },
	{ 132, "service option not supported" },
	{ 133, "requested service option not subscribed" },
	{ 134, "service option temporarily out of order" },
	{ 149, "PDP authentication failure" },
	{ 400, "generic undocumented error" },
	{ 401, "wrong state" },
	{ 402, "wrong mode" },
	{ 403, "context already activated" },
	{ 404, "stack already active" },
	{ 405, "activation failed" },
	{ 406, "context not opened" },
	{ 407, "cannot setup socket" },
	{ 408, "cannot resolve DN" },
	{ 409, "timeout in opening socket" },
	{ 410, "cannot open socket" },
	{ 411, "remote disconnected or timeout" },
	{ 412, "connection failed" },

	/* CMS errors */
	{ 300, "ME failure" },
	{ 301, "SMS service of ME reserved" },
	{ 302, "operation not allowed" },
	{ 303, "operation not supported" },
	{ 304, "invalid PDU mode parameter" },
	{ 305, "invalid text mode parameter" },
	{ 310, "SIM not inserted" },
	{ 311, "SIM PIN required" },
	{ 312, "PH-SIM PIN required" },
	{ 313, "SIM failure" },
	{ 314, "SIM busy" },
	{ 315, "SIM wrong" },
	{ 316, "SIM PUK required" },
	{ 317, "SIM PIN2 required" },
	{ 318, "SIM PUK2 required" },
	{ 320, "memory failure" },
	{ 321, "invalid memory index" },
	{ 322, "memory full" },
	{ 330, "SMSC address unknown" },
	{ 331, "no network service" },
	{ 332, "network timeout" },
	{ 340, "no +CNMA acknowledgment expected" },
	{ 500, "unknown error" },
};

static const char *vgsm_error_to_text(int code)
{
	int i;
	for (i=0; i<ARRAY_SIZE(vgsm_error_codes); i++) {
		if (vgsm_error_codes[i].code == code)
			return vgsm_error_codes[i].text;
	}

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

	intf->status = VGSM_INTF_STATUS_UNINITIALIZED;

	intf->connect_check_sched_id = -1;
	intf->monitor_thread = AST_PTHREADT_NULL;

	return intf;
}

static struct vgsm_interface *vgsm_intf_get(
	struct vgsm_interface *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	intf->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return intf;
}

static void vgsm_intf_put(
	struct vgsm_interface *intf)
{
	ast_mutex_lock(&vgsm.usecnt_lock);
	intf->refcnt--;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!intf->refcnt) {
		if (intf->lockdown_reason)
			free(intf->lockdown_reason);

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

//-----------------------------------------------------------------------------

static int do_vgsm_pin_set(int fd, int argc, char *argv[])
{
	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		return -1;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing OLDPIN\n");
		return -1;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "OLDPIN contains invalid characters\n");
		return -1;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		return -1;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		return -1;
	}

	if (!strcasecmp(argv[5], "enabled")) {
		vgsm_send_request(&intf->comm, 180 * SEC,
			"AT+CLCK=SC,1,\"%s\"", argv[4]);
	} else if (!strcasecmp(argv[5], "disabled")) {
		vgsm_send_request(&intf->comm, 180 * SEC,
			"AT+CLCK=SC,0,\"%s\"", argv[4]);
	} else {
		if (vgsm_validate_pin(argv[5]) < 0) {
			ast_cli(fd, "NEWPIN contains invalid characters\n");
			vgsm_intf_put(intf);
			return -1;
		}

		vgsm_send_request(&intf->comm, 180 * SEC,
			"AT+CPWD=SC,\"%s\",\"%s\"",
			argv[4], argv[5]);
	}

	int err = vgsm_expect_ok(&intf->comm);
	if (err != VGSM_RESP_OK) {
		ast_cli(fd, "Unable to complete command: %s (%d)\n",
			vgsm_error_to_text(err),
			err);
		vgsm_intf_put(intf);
		return -1;
	}

	vgsm_intf_put(intf);
	intf = NULL;

	return 0;
}

static char vgsm_pin_set_help[] =
	"Usage: vgsm pin set <interface> <OLDPIN> [<NEWPIN>|enabled|disabled]\n"
	"	Set PIN on selected interface.\n";

static struct ast_cli_entry vgsm_pin_set =
{
	{ "vgsm", "pin", "set", NULL },
	do_vgsm_pin_set,
	"Set PIN on the selected interface",
	vgsm_pin_set_help,
	NULL
};

//-----------------------------------------------------------------------------

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
	dst->operator_selection = src->operator_selection;

	strncpy(dst->operator_id, src->operator_id,
		sizeof(dst->operator_id));
}

static struct vgsm_urc urcs[];

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
				// Do something
				return;
			}

			strncpy(intf->name, cat, sizeof(intf->name));
			vgsm_copy_interface_config(intf, &vgsm.default_intf);

			vgsm_comm_init(&intf->comm, urcs);

			list_add_tail(&intf->ifs_node, &vgsm.ifs);
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
			"Unable to load config %s, VGSM disabled\n",
			VGSM_OP_CONFIG_FILE);

		return;
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
}

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return 0;
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
	vgsm.debug = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return 0;
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

static int do_show_vgsm_interfaces(int fd, int argc, char *argv[])
{
	struct vgsm_interface *intf;

	ast_mutex_lock(&vgsm.lock);
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		ast_cli(fd, "\n------ Interface '%s' ---------\n", intf->name);

		ast_cli(fd,
			"\n"
		      	"  Device : %s\n"
			"  Context: %s\n"
			"  RX-gain: %d\n"
			"  TX-gain: %d\n"
			"  Set clock: %s\n",
			intf->device_filename,
			intf->context,
			intf->rx_gain,
			intf->tx_gain,
			intf->set_clock ? "YES" : "NO");

		ast_cli(fd,
			"\nModule:\n"
			"  Status: %s\n",
			vgsm_intf_status_to_text(intf->status));

		if (intf->status == VGSM_INTF_STATUS_UNINITIALIZED ||
		    intf->status == VGSM_INTF_STATUS_INITIALIZING)
			continue;

		if (intf->status == VGSM_INTF_STATUS_LOCKED_DOWN ||
		    intf->status == VGSM_INTF_STATUS_FAILED ||
		    intf->status == VGSM_INTF_STATUS_WAITING_PIN) {
			ast_cli(fd, "  Reason: %s\n", intf->lockdown_reason);
			continue;
		}

		ast_cli(fd,
			"  Model: %s %s\n"
			"  Version: %s\n"
			"  DOB Version: %s\n"
			"  Serial#: %s\n"
			"  IMEI: %s\n",
			intf->module.vendor,
			intf->module.model,
			intf->module.version,
			intf->module.dob_version,
			intf->module.serial_number,
			intf->module.imei);

		if (intf->sim.inserted) {
			ast_cli(fd,
				"\nSIM:\n"
				"  IMSI: %s\n"
				"  PIN remaining attempts: %d\n",
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
			continue;

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

		ast_cli(fd,
			"  Local Area Code: 0x%04x\n"
			"  Cell ID: 0x%04x\n"
			"  Base station ID: %d\n"
			"  Time Advance: %d\n"
			"  Assigned radio channel: %d\n"
			"  Rx Power: %d dBm\n",
			intf->net.cells[0].lac,
			intf->net.cells[0].id,
			intf->net.bsic,
			intf->net.ta,
			intf->net.cells[0].arfcn,
			intf->net.cells[0].pwr);

		ast_cli(fd, "  --- %d adjacent cells\n",
			intf->net.ncells - 1);

		int i;
		for (i=1; i<intf->net.ncells; i++) {
			ast_cli(fd,
				"  %d: LAC: 0x%04x"
				" ID: 0x%04x"
				" ARFCN: %-4d"
				" PWR: %d dBm\n",
				i,
				intf->net.cells[i].lac,
				intf->net.cells[i].id,
				intf->net.cells[i].arfcn,
				intf->net.cells[i].pwr);
		}
	}
	ast_mutex_unlock(&vgsm.lock);

	return 0;
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
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_reload(int fd, int argc, char *argv[])
{
	vgsm_reload_config();

	return 0;
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

static int do_vgsm_pin_input(int fd, int argc, char *argv[])
{
	int res;

	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		return -1;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PIN\n");
		return -1;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "PIN contains invalid characters\n");
		return -1;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		return -1;
	}

	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	vgsm_send_request(comm, 20 * SEC, "AT+CPIN");
	resp = vgsm_read_response(comm);
	if (!resp) {
		ast_cli(fd, "Communication error");
		return -1;
	}

	const struct vgsm_response_line *first_line;
	first_line = vgsm_response_first_line(resp);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		vgsm_send_request(comm, 20 * SEC,
				"AT+CPIN=\"%s\"", argv[4]);

		int err = vgsm_expect_ok(&intf->comm);
		if (err != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_error_to_text(err),
				err);
			res = -1;
		}

		intf->status = VGSM_INTF_STATUS_WAITING_INITIALIZATION;

	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		ast_cli(fd, "SIM requires PUK");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 10")) {
		ast_cli(fd, "SIM not present");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 13")) {
		ast_cli(fd, "SIM defective");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 14")) {
		ast_cli(fd, "SIM busy");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 15")) {
		ast_cli(fd, "Wrong type of SIM");
		res = -1;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_response_put(resp);
	vgsm_intf_put(intf);
	intf = NULL;

	return 0;
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
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_puk_input(int fd, int argc, char *argv[])
{
	int res;

	if (argc < 4) {
		ast_cli(fd, "Missing interface name\n");
		return -1;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PUK\n");
		return -1;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "PUK contains invalid characters\n");
		return -1;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		return -1;
	}

	if (vgsm_validate_pin(argv[4]) < 0) {
		ast_cli(fd, "NEWPIN contains invalid characters\n");
		return -1;
	}

	struct vgsm_interface *intf;
	intf = vgsm_intf_get_by_name(argv[3]);
	if (!intf) {
		ast_cli(fd, "Cannot find interface '%s'\n", argv[3]);
		return -1;
	}

	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	vgsm_send_request(comm, 20 * SEC, "AT+CPIN");
	resp = vgsm_read_response(comm);
	if (!resp) {
		ast_cli(fd, "Communication error");
		return -1;
	}

	const struct vgsm_response_line *first_line;
	first_line = vgsm_response_first_line(resp);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {
		ast_cli(fd, "SIM requires PIN");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {

		vgsm_send_request(comm, 20 * SEC,
				"AT+CPIN=\"%s\",\"%s\"", argv[4], argv[5]);

		int err = vgsm_expect_ok(&intf->comm);
		if (err != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_error_to_text(err),
				err);
			res = -1;
		}

		intf->status = VGSM_INTF_STATUS_WAITING_INITIALIZATION;

	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 10")) {
		ast_cli(fd, "SIM not present");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 13")) {
		ast_cli(fd, "SIM defective");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 14")) {
		ast_cli(fd, "SIM busy");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 15")) {
		ast_cli(fd, "Wrong type of SIM");
		res = -1;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_response_put(resp);
	vgsm_intf_put(intf);
	intf = NULL;

	return 0;
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
	NULL
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

	vgsm_debug("Connecting streamport %06d to chan %06d\n",
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
		goto err_ioctl_connect_path;
	}

	vgsm_chan->path_id = vc.path_id;

	memset(&vc, 0, sizeof(vc));
	vc.path_id = vgsm_chan->path_id;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_ENABLE_PATH,
						(caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_ENABLE_PATH, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_enable_path;
	}

	return 0;

err_ioctl_enable_path:
err_ioctl_connect_path:
err_ioctl_visdn_get_chanid:
	close(vgsm_chan->sp_fd);
err_open_streamport:
err_ioctl_vgsm_get_chanid:

	return -1;
}


static int vgsm_connect_check(void *data)
{
	struct vgsm_interface *intf = data;
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	if (!intf->current_call) {
		ast_log(LOG_ERROR,
			"Unexpected connect_check with no current_call\n");
		return -1;
	}

	vgsm_send_request(comm, 5 * SEC, "AT+CPAS");
	resp = vgsm_read_response(comm);
	if (!resp)
		return -1;

	int status;
	const char *first_line = vgsm_response_first_line(resp)->text;
	if (strstr(first_line, "+CPAS: ")) {
		status = atoi(first_line + strlen("+CPAS :"));
	} else {
		ast_log(LOG_ERROR, "Unexpected response %s to AT+CPAS\n",
			first_line);
		vgsm_response_put(resp);
		return -1;
	}

	vgsm_response_put(resp);

	if (status == 0)
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
	else if (status == 4) {
//		struct vgsm_chan *vgsm_chan = to_vgsm_chan(intf->current_call);
//		vgsm_connect_channel(vgsm_chan);

		ast_queue_control(intf->current_call, AST_CONTROL_ANSWER);
	} else {
		intf->connect_check_sched_id =
			ast_sched_add(sched, 1000, vgsm_connect_check, intf);
		vgsm_comm_awake(&intf->comm);
	}

	return 0;
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
	if (intf->status != VGSM_INTF_STATUS_READY) {
		ast_mutex_unlock(&intf->lock);
		err = -1;
		goto err_int_not_ready;
	}

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	intf->status = VGSM_INTF_STATUS_INCALL;
	intf->current_call = ast_chan;
	vgsm_chan->intf = vgsm_intf_get(intf);

	ast_mutex_unlock(&intf->lock);

	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", intf->name, 1);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	struct vgsm_response *resp;
	// 'timeout' instead of 20s ?
	vgsm_send_request(&intf->comm, 180 * SEC, "ATD%s;", number);
	resp = vgsm_read_response(&intf->comm);
	if (!resp) {
		err = -1;
		goto err_atd_failed;
	}

	vgsm_connect_channel(vgsm_chan);

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);
	ast_setstate(ast_chan, AST_STATE_UP);

	intf->connect_check_sched_id =
		ast_sched_add(sched, 1000, vgsm_connect_check, intf);
	vgsm_comm_awake(&intf->comm);

	vgsm_intf_put(intf);

	return 0;

err_atd_failed:
err_int_not_ready:
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

	vgsm_debug("vgsm_answer\n");

	ast_indicate(ast_chan, -1);

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return -1;
	}

	ast_channel_setwhentohangup(intf->current_call, 0);

	struct vgsm_response *resp;
	vgsm_send_request(&intf->comm, 1 * SEC, "ATA");
	resp = vgsm_read_response(&intf->comm);
	if (!resp)
		return -1;

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

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return 1;
	}

	vgsm_debug("vgsm_indicate %d\n", condition);

	switch(condition) {
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
		return 1;
	break;

	case -1:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_OFFHOOK: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "dial");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_HANGUP: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "congestion");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_RINGING: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "ring");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_ANSWER:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_BUSY: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "busy");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_CONGESTION: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "busy");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_PROGRESS:
		return 0;
	break;

	case AST_CONTROL_PROCEEDING:
		return 0;
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

static void vgsm_disconnect_channel(
	struct vgsm_chan *vgsm_chan)
{
	ast_mutex_lock(&vgsm_chan->ast_chan->lock);

	if (vgsm_chan->sp_fd < 0)
		return;

	struct visdn_connect vc;
	vc.path_id = vgsm_chan->path_id;
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
	vgsm_debug("vgsm_hangup %s\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_interface *intf = vgsm_chan->intf;

	if (intf) {
		ast_mutex_lock(&intf->lock);
		if (intf->connect_check_sched_id >= 0)
			ast_sched_del(sched, intf->connect_check_sched_id);
		ast_mutex_unlock(&intf->lock);

		vgsm_send_request(&intf->comm, 5 * SEC, "ATH");
		vgsm_expect_ok(&intf->comm);

		intf->status = VGSM_INTF_STATUS_READY;
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

	ast_setstate(ast_chan, AST_STATE_DOWN);

	ast_mutex_unlock(&ast_chan->lock);

	vgsm_debug("vgsm_hangup complete\n");

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

	return &f;
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

//	ast_chan->language[0] = '\0';
//	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	vgsm_chan->ast_chan = ast_chan;
	ast_chan->tech_pvt = vgsm_chan;

	ast_chan->tech = &vgsm_tech;

	ast_setstate(ast_chan, state);

	return ast_chan;

	close(ast_chan->fds[0]);
err_open_timer:
	ast_hangup(ast_chan);
err_channel_alloc:

	return NULL;
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

// Must be called with vgsm.lock acquired
static void vgsm_add_interface(const char *name)
{
	int found = FALSE;
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcasecmp(intf->name, name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		intf = malloc(sizeof(*intf));

		strncpy(intf->name, name, sizeof(intf->name));
		vgsm_copy_interface_config(intf, &vgsm.default_intf);

		list_add_tail(&intf->ifs_node, &vgsm.ifs);
	}
}

// Must be called with vgsm.lock acquired
static void vgsm_rem_interface(const char *name)
{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcmp(intf->name, name))
			break;
	}
}

#define to_intf(cm) container_of((cm), struct vgsm_interface, comm)

static void handle_unsolicited_busy(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);

	ast_mutex_lock(&intf->lock);
	if (intf->current_call)
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_no_dialtone(
	const struct vgsm_response *urm)
{
	ast_log(LOG_WARNING, "'NO DIALTONE' not yet handled\n");
}

static void handle_unsolicited_no_carrier(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);

	ast_mutex_lock(&intf->lock);
	if (intf->current_call)
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_ring(
	const struct vgsm_response *urm)
{
	ast_log(LOG_NOTICE, "Unexpected RING\n");
}

static void handle_unsolicited_cring(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_response_first_line(urm)->text;
	const char *pars = line + strlen(urm->urc->code);

	ast_mutex_lock(&intf->lock);

	if (intf->status != VGSM_INTF_STATUS_READY) {
		ast_log(LOG_NOTICE,
			"Rejecting RING on not ready interface\n");
		vgsm_send_request(comm, 5 * SEC, "ATH");
		vgsm_expect_ok(comm);

		goto err_intf_not_ready;
	}

	if (intf->current_call) {
		ast_log(LOG_NOTICE, "Updating timeout\n");
		ast_channel_setwhentohangup(intf->current_call, 6);
		goto timeout_updated;
	}

	if (strcmp(pars, "VOICE")) {
		ast_log(LOG_NOTICE, "Not a voice call, rejecting\n");
		vgsm_send_request(comm, 5 * SEC, "ATH");
		vgsm_expect_ok(comm);

		goto err_not_voice;
	}

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

	intf->current_call = ast_chan;

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	if (strlen(intf->last_cli.number))
		ast_chan->cid.cid_num = strdup(intf->last_cli.number);

	if (strlen(intf->last_cli.alpha))
		ast_chan->cid.cid_name = strdup(intf->last_cli.alpha);

	ast_chan->cid.cid_pres = AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
	ast_chan->cid.cid_ton = intf->last_cli.ton;

	strcpy(ast_chan->exten, "s");
	strncpy(ast_chan->context, intf->context, sizeof(ast_chan->context));
	ast_chan->priority = 1;

	snprintf(ast_chan->name, sizeof(ast_chan->name),
		"VGSM/%s/%d", intf->name, 1);

	ast_channel_setwhentohangup(ast_chan, 6);

	ast_setstate(ast_chan, AST_STATE_RING);

	if (ast_pbx_start(ast_chan)) {
		ast_log(LOG_ERROR,
			"Unable to start PBX on %s\n",
			ast_chan->name);
			goto err_pbx_start;

		vgsm_send_request(comm, 5 * SEC, "ATH");
	}

	ast_mutex_unlock(&intf->lock);

	return;

err_pbx_start:
	ast_hangup(ast_chan);
err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:
err_not_voice:
timeout_updated:
err_intf_not_ready:

	ast_mutex_unlock(&intf->lock);

	return;
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

	if (token_p > token)
		return token;
	else
		return NULL;
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
		if (get_token(&pars_ptr, field, sizeof(field))) {
			if (sscanf(field, "%x",
					&intf->net.cells[0].lac) != 1)

				ast_log(LOG_WARNING,
					"Cannot parse LAC '%s'\n", field);
		}

		if (get_token(&pars_ptr, field, sizeof(field))) {
			if (sscanf(field, "%x", &intf->net.cells[0].id) != 1)
				ast_log(LOG_WARNING,
					"Cannot parse CI '%s'\n", field);
		}
	}

	return;

err_no_status:
err_no_mode:

	return;
}

static int vgsm_update_cops(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	vgsm_send_request(&intf->comm, 180 * SEC, "AT+COPS?");
	resp = vgsm_read_response(comm);
	if (!resp)
		return -1;

	char field[10];
	const char *line = vgsm_response_first_line(resp)->text;
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
	vgsm_response_put(resp);

	return 0;
}

static void vgsm_module_update_readiness(
	struct vgsm_interface *intf)
{
	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		intf->status = VGSM_INTF_STATUS_READY;
	} else {
		intf->status = VGSM_INTF_STATUS_NO_NET;
	}
}

static void handle_unsolicited_creg(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_response_first_line(urm)->text;
	const char *pars = line + strlen(urm->urc->code);

	ast_mutex_lock(&intf->lock);

	vgsm_update_intf_by_creg(intf, pars, FALSE);

	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_log(LOG_NOTICE,
			"Module '%s' registration %s (LAC=0x%04x, CI=0x%04x)\n",
			intf->name,
			vgsm_net_status_to_text(intf->net.status),
			intf->net.cells[0].lac,
			intf->net.cells[0].id);

		vgsm_update_cops(intf);
	} else {
		ast_log(LOG_NOTICE,
			"Module '%s' registration %s\n",
			intf->name,
			vgsm_net_status_to_text(intf->net.status));
	}

	vgsm_module_update_readiness(intf);

	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_cusd(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cccm(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_ccwa(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_clip(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_response_first_line(urm)->text;
	const char *pars = line + strlen(urm->urc->code);
	const char *pars_ptr = pars;
	char field[32];

	ast_mutex_lock(&intf->lock);

	if (!get_token(&pars_ptr, intf->last_cli.number,
			sizeof(intf->last_cli.number))) {

		ast_log(LOG_WARNING, "Cannot parse CID '%s'\n", pars);
		goto err_parse_cid;
	}

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	intf->last_cli.ton = atoi(field);

	if (!get_token(&pars_ptr, intf->last_cli.subaddress,
				sizeof(intf->last_cli.subaddress)))
		goto parsing_done;

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	intf->last_cli.subaddress_type = atoi(field);

	if (!get_token(&pars_ptr, intf->last_cli.alpha,
				sizeof(intf->last_cli.alpha)))
		goto parsing_done;

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto parsing_done;

	intf->last_cli.validity = atoi(field);

parsing_done:

	ast_mutex_unlock(&intf->lock);
	return;

err_parse_cid:

	ast_mutex_unlock(&intf->lock);
	return;
}

static void handle_unsolicited_cssi(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cssu(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_alarm(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cgreg(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cmti(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cmt(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cbm(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_cds(
	const struct vgsm_response *urm)
{
}

static void handle_unsolicited_qss(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);
	const char *line = vgsm_response_first_line(urm)->text;
	const char *pars = line + strlen(urm->urc->code);

	ast_mutex_lock(&intf->lock);
	if (atoi(pars))
		intf->sim.inserted = TRUE;
	else
		intf->sim.inserted = FALSE;
	ast_mutex_unlock(&intf->lock);
}

static void handle_unsolicited_jdr(
	const struct vgsm_response *urm)
{
	struct vgsm_comm *comm = urm->comm;
	struct vgsm_interface *intf = to_intf(comm);

	ast_mutex_lock(&intf->lock);
	ast_log(LOG_WARNING,
		"Jammin'... jammin' in the name of the flying "
		"spaghetti monster.\n");

	vgsm_module_update_readiness(intf);
	ast_mutex_unlock(&intf->lock);
}

static struct vgsm_urc urcs[] =
{
	{ "BUSY", FALSE, handle_unsolicited_busy },
	{ "NO CARRIER", FALSE, handle_unsolicited_no_carrier },
	{ "NO DIALTONE", FALSE, handle_unsolicited_no_dialtone },
	{ "BUSY", FALSE, handle_unsolicited_busy },
	{ "RING: ", FALSE, handle_unsolicited_ring },
	{ "+CRING: ", FALSE, handle_unsolicited_cring },
	{ "+CREG: ", FALSE, handle_unsolicited_creg },
	{ "+CUSD: ", FALSE, handle_unsolicited_cusd },
	{ "+CCWA: ", FALSE, handle_unsolicited_ccwa },
	{ "+CLIP: ", FALSE, handle_unsolicited_clip },
	{ "+CCCM: ", FALSE, handle_unsolicited_cccm },
	{ "+CSSI: ", FALSE, handle_unsolicited_cssi },
	{ "+CSSU: ", FALSE, handle_unsolicited_cssu },
	{ "+ALARM: ", FALSE, handle_unsolicited_alarm },
	{ "+CGREG: ", FALSE, handle_unsolicited_cgreg },
	{ "+CMTI: ", FALSE, handle_unsolicited_cmti },
	{ "+CMT: ", FALSE, handle_unsolicited_cmt },
	{ "+CBM: ", TRUE, handle_unsolicited_cbm },
	{ "+CDS: ", FALSE, handle_unsolicited_cds },
	{ "#QSS: ", FALSE, handle_unsolicited_qss },
	{ "#JDR: ", FALSE, handle_unsolicited_jdr },
	{ },
};

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

static int vgsm_pin_check_and_input(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;
	const struct vgsm_response_line *first_line;
	int res = 0;

	/* Be careful to not consume all the available attempts */
	vgsm_send_request(comm, 10 * SEC, "AT#PCT");

	resp = vgsm_read_response(comm);
	if (!resp) {
		vgsm_intf_setreason(intf, "Communication error");
		vgsm_comm_set_bitbucket(&intf->comm);
		intf->status = VGSM_INTF_STATUS_FAILED;
		return -1;
	}

	intf->sim.remaining_attempts =
		atoi(vgsm_response_first_line(resp)->text + strlen("#PCT: "));

	vgsm_response_put(resp);

	vgsm_send_request(comm, 20 * SEC, "AT+CPIN");
	resp = vgsm_read_response(comm);
	if (!resp) {
		vgsm_intf_setreason(intf, "Communication error");
		vgsm_comm_set_bitbucket(&intf->comm);
		intf->status = VGSM_INTF_STATUS_FAILED;
		return -1;
	}

	first_line = vgsm_response_first_line(resp);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		/* Do nothing */
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		if (intf->sim.remaining_attempts < 3) {
			intf->status = VGSM_INTF_STATUS_WAITING_PIN;
			vgsm_intf_setreason(intf, "Input PIN manually");
			res = -1;
		} else {
			vgsm_send_request(comm, 20 * SEC,
					"AT+CPIN=\"%s\"", intf->pin);

			if (vgsm_expect_ok(comm) < 0) {
				intf->status = VGSM_INTF_STATUS_WAITING_PIN;
				vgsm_intf_setreason(intf,
					"SIM PIN refused, input manually");
				res = -1;
			}
		}
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		intf->status = VGSM_INTF_STATUS_WAITING_PIN;
		vgsm_intf_setreason(intf, "SIM requires PIN2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		intf->status = VGSM_INTF_STATUS_WAITING_PIN;
		vgsm_intf_setreason(intf, "SIM requires PUK, input manually");
		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		intf->status = VGSM_INTF_STATUS_WAITING_PIN;
		vgsm_intf_setreason(intf, "SIM requires PUK2");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 10")) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf, "SIM not present");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 13")) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf, "SIM defective");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 14")) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf, "SIM busy");
		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 15")) {
		intf->status = VGSM_INTF_STATUS_WAITING_PIN;
		vgsm_intf_setreason(intf, "Wrong type of SIM");
		res = -1;
	} else {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf,
			"Unknown response '%s'", first_line->text);

		res = -1;
	}

	vgsm_response_put(resp);

	return res;
}

static int vgsm_module_codec_init(struct vgsm_interface *intf)
{
	struct vgsm_codec_ctl cctl;

	if (ioctl(intf->comm.fd, VGSM_IOC_POWER_SET, 1) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_POWER_SET) failed: %s\n",
			strerror(errno));

		return -1;
	}

	cctl.parameter = VGSM_CODEC_RESET;
	if (ioctl(intf->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_CODEC_SET, RESET) failed: %s\n",
			strerror(errno));

		return -1;
	}

	cctl.parameter = VGSM_CODEC_RXGAIN;
	cctl.value = intf->rx_gain;

	if (ioctl(intf->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_CODEC_SET, RXGAIN) failed: %s\n",
			strerror(errno));

		return -1;
	}

	cctl.parameter = VGSM_CODEC_TXGAIN;
	cctl.value = intf->tx_gain;

	if (ioctl(intf->comm.fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_CODEC_SET, TXGAIN) failed: %s\n",
			strerror(errno));

		return -1;
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
}

static int vgsm_module_init(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;

	/* Very basic onfiguration before entering PIN */

	/* "AT" is needed before AT+IPR, otherwise it
	 * responds with ERROR */

	vgsm_send_request(comm, 200 * MILLISEC, "AT");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	vgsm_send_request(comm, 200 * MILLISEC, "AT+IPR=38400");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	vgsm_send_request(comm, 5 * SEC, "AT+CMEE=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	return 0;

err_no_resp:

	return -1;
}

static int vgsm_module_configure(struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;

	/* Configure operator selection */
	switch(intf->operator_selection) {
	case VGSM_OPSEL_AUTOMATIC:
		vgsm_send_request(comm, 180 * SEC, "AT+COPS=0,2");
	break;

	case VGSM_OPSEL_MANUAL_UNLOCKED:
		vgsm_send_request(comm, 180 * SEC, "AT+COPS=1,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_FALLBACK:
		vgsm_send_request(comm, 180 * SEC, "AT+COPS=4,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_LOCKED:
		vgsm_send_request(comm, 180 * SEC, "AT+COPS=5,2,%s",
			intf->operator_id);
	break;
	}

	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable unsolicited registration informations */
	vgsm_send_request(comm, 5 * SEC, "AT+CREG=2");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable unsolicited unstructured supplementary data */
	vgsm_send_request(comm, 180 * SEC, "AT+CUSD=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable unsolicited GPRS registration status */
	vgsm_send_request(comm, 5 * SEC, "AT+CGREG=2");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable unsolicited supplementary service notification */
	vgsm_send_request(comm, 20 * SEC, "AT+CSSN=1,1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Subscribe to all cell broadcast channels */
	vgsm_send_request(comm, 100 * MILLISEC, "AT+CSCB=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable extended cellular result codes */
	vgsm_send_request(comm, 200 * MILLISEC, "AT+CRC=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable Calling Line Presentation */
	vgsm_send_request(comm, 180 * SEC, "AT+CLIP=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable unsolicited advice of charge notifications */
	vgsm_send_request(comm, 20 * SEC, "AT+CAOC=2");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Sets current time on module */
	if (intf->set_clock) {
		struct tm *tm;
		time_t ct = time(NULL);

		tm = localtime(&ct);

		vgsm_send_request(comm, 200 * MILLISEC,
			"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03ld\"",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			-(timezone / 3600) + tm->tm_isdst);
		if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
			goto err_no_resp;
	}

	/* Enable unsolicited new message indications */
	vgsm_send_request(comm, 5 * SEC, "AT+CNMI=2,1,2,1,0");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Select handsfree audio path */
	vgsm_send_request(comm, 10 * SEC, "AT#CAP=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Disable tones */
	vgsm_send_request(comm, 10 * SEC, "AT#STM=0");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Set ringer auto directed to GPIO7 */
	vgsm_send_request(comm, 10 * SEC, "AT#SRP=3");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

// SET #SGPO
	/* Enable unsolicited SIM status reporting */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#QSS=1");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Disable handsfree echo canceller */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#SHFEC=0");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Set MIC audio gain to +0dB */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#HFMICG=4");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Disable sidetone generation */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#SHFSD=0");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Set GSM1800 band */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#BND=0");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	/* Enable Jammer detector */
	vgsm_send_request(comm, 100 * MILLISEC, "AT#JDR=2");
	if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
		goto err_no_resp;

	return 0;

err_no_resp:

	return -1;
}

static int vgsm_module_update_static_info(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	ast_mutex_lock(&intf->lock);

	/*--------*/
	vgsm_send_request(comm, 5 * SEC, "AT+CGMI");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.vendor,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.vendor));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(comm, 5 * SEC, "AT+CGMM");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.model,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.model));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(comm, 5 * SEC, "ATI5");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.dob_version,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.dob_version));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(comm, 5 * SEC, "AT+CGMR");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.version,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.version));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(comm, 5 * SEC, "AT+GSN");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.serial_number,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.serial_number));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(comm, 20 * SEC, "AT+CGSN");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->module.imei,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.imei));

	vgsm_response_put(resp);

	
	/*--------*/
	vgsm_send_request(comm, 100 * MILLISEC, "AT#QSS");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	if (strlen(vgsm_response_first_line(resp)->text) > 7) {
		if (!strcmp(vgsm_response_first_line(resp)->text + 6, "1,1"))
			intf->sim.inserted = TRUE;
		else
			intf->sim.inserted = FALSE;
	}

	vgsm_response_put(resp);

	if (!intf->sim.inserted)
		goto no_sim;
	
	/*--------*/
	vgsm_send_request(comm, 20 * SEC, "AT+CIMI");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_no_resp;

	strncpy(intf->sim.imsi,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->sim.imsi));

	vgsm_response_put(resp);

no_sim:

	ast_mutex_unlock(&intf->lock);

	return 0;

err_no_resp:

	ast_mutex_unlock(&intf->lock);

	return -1;
}

static int vgsm_module_update_net_info(
	struct vgsm_interface *intf)
{
	struct vgsm_comm *comm = &intf->comm;
	struct vgsm_response *resp;

	ast_mutex_lock(&intf->lock);

	vgsm_send_request(comm, 5 * SEC, "AT+CREG?");
	resp = vgsm_read_response(comm);
	if (!resp)
		goto err_creg_read_response;

	const char *line = vgsm_response_first_line(resp)->text;
	const char *field;

	if (strlen(line) > 7)
		vgsm_update_intf_by_creg(intf, line + 7, TRUE);

	vgsm_response_put(resp);

	vgsm_module_update_readiness(intf);

err_creg_read_response:

	intf->net.ncells = 0;

	if (intf->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    intf->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING)
		goto not_registered;

	int i;
	for (i=0; i<ARRAY_SIZE(intf->net.cells); i++) {
		vgsm_send_request(comm, 10 * SEC, "AT#MONI=%d", i);
		if (vgsm_expect_ok(comm) != VGSM_RESP_OK)
			break;

		vgsm_send_request(comm, 10 * SEC, "AT#MONI");
		resp = vgsm_read_response(comm);
		if (!resp)
			break;

		line = vgsm_response_first_line(resp)->text;

		if (vgsm_comm_line_error(line) != VGSM_RESP_UNKNOWN) {
			vgsm_response_put(resp);
			break;
		}

		if (i == 0) {
			field = strstr(line, "BSIC:");
			if (field) {
				field += strlen("BSIC:");
				sscanf(field, "%d", &intf->net.bsic);
			}

			field = strstr(line, "TA:");
			if (field) {
				field += strlen("TA:");
				sscanf(field, "%d", &intf->net.ta);
			}

			field = strstr(line, "RxQual:");
			if (field) {
				field += strlen("RxQual:");
				sscanf(field, "%d", &intf->net.rxqual);
			}
		}

		field = strstr(line, "LAC:");
		if (field) {
			field += strlen("LAC:");
			sscanf(field, "%04x", &intf->net.cells[i].lac);
		}

		field = strstr(line, "Id:");
		if (field) {
			field += strlen("Id:");
			sscanf(field, "%04x", &intf->net.cells[i].id);
		}

		field = strstr(line, "ARFCN:");
		if (field) {
			field += strlen("ARFCN:");
			sscanf(field, "%d", &intf->net.cells[i].arfcn);
		}

		field = strstr(line, "PWR:");
		if (field) {
			field += strlen("PWR:");
			sscanf(field, "%d", &intf->net.cells[i].pwr);
		}

		vgsm_response_put(resp);

		intf->net.ncells++;
	}

	vgsm_update_cops(intf);

not_registered:
	ast_mutex_unlock(&intf->lock);

	return 0;

/*	AT+CSQ
	AT#CSURV
*/
}

/***********************************************/

static void vgsm_module_initialize(
	struct vgsm_interface *intf)
{
	if (intf->comm.fd < 0) {
		intf->comm.fd = open(intf->device_filename, O_RDWR);
		if (intf->comm.fd < 0) {
			ast_log(LOG_WARNING,
				"Unable to open '%s': %s\n",
				intf->device_filename,
				strerror(errno));

			intf->status = VGSM_INTF_STATUS_FAILED;
			vgsm_comm_set_bitbucket(&intf->comm);
			return;
		}

		vgsm_comm_awake(&intf->comm);
	}

	intf->status = VGSM_INTF_STATUS_INITIALIZING;

	ast_log(LOG_NOTICE, "Initializing module '%s'\n", intf->name);

	if (vgsm_comm_start_recovery(&intf->comm) < 0) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf, "Communication error");
		return;
	}

	if (vgsm_module_codec_init(intf) < 0) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf,
			"Error configuring CODEC");
		return;
	}

	if (vgsm_module_init(intf) < 0) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf,
			"Error initializing module");
		return;
	}

	if (vgsm_pin_check_and_input(intf))
		return;

	if (vgsm_module_configure(intf) < 0) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf,
			"Error configuring module");

		return;
	}

	vgsm_module_update_static_info(intf);

	if (vgsm_module_update_net_info(intf) < 0) {
		intf->status = VGSM_INTF_STATUS_FAILED;
		vgsm_comm_set_bitbucket(&intf->comm);
		vgsm_intf_setreason(intf,
			"Error updating net informations");
		return;
	}

	vgsm_module_update_readiness(intf);

	ast_log(LOG_NOTICE, "Module '%s' successfully initialized\n",
		intf->name);

}

static int vgsm_module_monitor_thread_stuff(
	struct vgsm_interface *intf)
{
	switch(intf->status) {
	case VGSM_INTF_STATUS_UNINITIALIZED:
	case VGSM_INTF_STATUS_WAITING_INITIALIZATION:
		vgsm_module_initialize(intf);
		return 0;
	break;

	case VGSM_INTF_STATUS_FAILED:
		vgsm_module_initialize(intf);
		return 30;
	break;

	case VGSM_INTF_STATUS_READY:
	case VGSM_INTF_STATUS_NO_NET:
		vgsm_module_update_net_info(intf);
		return 0;
	break;

	case VGSM_INTF_STATUS_INITIALIZING:
		ast_log(LOG_ERROR, "Unexpected intf in Initialing state\n");
	break;

	case VGSM_INTF_STATUS_INCALL:
	case VGSM_INTF_STATUS_WAITING_PIN:
	case VGSM_INTF_STATUS_LOCKED_DOWN:
		// Do nothing
		return 30;
	break;
	}

	return 10;
}

static void *vgsm_module_monitor_thread_main(void *data)
{
	struct vgsm_interface *intf = (struct vgsm_interface *)data;

	sleep(2);

	for(;;) {
		int time_to_sleep;
		time_to_sleep = vgsm_module_monitor_thread_stuff(intf);
		sleep(time_to_sleep);
	}

	return NULL;
}

int load_module()
{
	int err;

	memset(&vgsm, 0, sizeof(vgsm));
	
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

	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		err = -1;
		goto err_sched_context_create;
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
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&show_vgsm_interfaces);
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

	return 0;

	// Kill comm thread
err_comm_thread_create:
	ast_channel_unregister(&vgsm_tech);
err_channel_register:
	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	sched_context_destroy(sched);
err_sched_context_create:
	close(vgsm.router_control_fd);
err_open_router_control:

	return err;
}

int unload_module(void)
{
	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	if (sched)
		sched_context_destroy(sched);

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

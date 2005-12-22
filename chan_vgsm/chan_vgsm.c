/*
 * vGSM channel driver for Asterisk
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
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <sys/termios.h>
#include <sys/signal.h>
#include <ctype.h>

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
#include <linux/visdn/softcxc.h>

#include "chan_vgsm.h"

//#include "../config.h"

#define VGSM_DESCRIPTION "VoiSmart VGSM Channel For Asterisk"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"
#define VGSM_OP_CONFIG_FILE "vgsm_operators.conf"

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static pthread_t vgsm_comm_thread = AST_PTHREADT_NULL;
static pthread_t vgsm_monitor_thread = AST_PTHREADT_NULL;

static struct sched_context *sched;

struct vgsm_operator_info
{
	struct list_head node;

	char id[8];
	const char *name;
	const char *country;
	const char *date;
	const char *bands;
};

enum vgsm_interface_status
{
	VGSM_INT_STATUS_UNINITIALIZED,
	VGSM_INT_STATUS_INITIALIZING,
	VGSM_INT_STATUS_READY,
	VGSM_INT_STATUS_INCALL,
	VGSM_INT_STATUS_NOT_READY,
	VGSM_INT_STATUS_LOCKED_DOWN,
	VGSM_INT_STATUS_FAILED,
};

enum vgsm_net_status
{
	VGSM_NET_STATUS_NOT_SEARCHING,
	VGSM_NET_STATUS_NOT_REGISTERED,
	VGSM_NET_STATUS_REGISTERED_HOME,
	VGSM_NET_STATUS_UNKNOWN,
	VGSM_NET_STATUS_REGISTRATION_DENIED,
	VGSM_NET_STATUS_REGISTERED_ROAMING,
};

enum vgsm_operator_selection
{
	VGSM_OPSEL_AUTOMATIC = 0,
	VGSM_OPSEL_MANUAL_UNLOCKED = 1,
	VGSM_OPSEL_MANUAL_FALLBACK = 4,
	VGSM_OPSEL_MANUAL_LOCKED = 5,
};

enum vgsm_operator_status
{
	VGSM_OPSTAT_UNKNOWN,
	VGSM_OPSTAT_AVAILABLE,
	VGSM_OPSTAT_CURRENT,
	VGSM_OPSTAT_FORBIDDEN,
};

enum vgsm_parser_state
{
	VGSM_PS_BITBUCKET,
	VGSM_PS_IDLE,
	VGSM_PS_RECOVERING,
	VGSM_PS_AWAITING_RESPONSE,
	VGSM_PS_READING_RESPONSE,
	VGSM_PS_RESPONSE_READY,
	VGSM_PS_READING_URC,
	VGSM_PS_AWAITING_RESPONSE_READING_URC,
	VGSM_PS_RESPONSE_READY_READING_URC,
};

typedef long long longtime_t;

struct vgsm_response
{
	int refcnt;

	struct list_head lines;
};

struct vgsm_response_line
{
	struct list_head node;

	char text[0];
};

struct vgsm_interface;
struct vgsm_urc
{
	const char *code;

	void (*handler)(
		const struct vgsm_urc *urc,
		struct vgsm_interface *intf,
		const char *line,
		const char *pars);
};

struct vgsm_interface
{
	struct list_head ifs_node;

	ast_mutex_t lock;

	/* Configuration */
	char name[64];
	char device_filename[PATH_MAX];

	char context[AST_MAX_EXTENSION];

	char pin[16];
	int min_level;
	int rx_gain;
	int tx_gain;
	enum vgsm_operator_selection operator_selection;
	char operator_id[8];
	int set_clock;

	/* Operative data */

	int fd;

	enum vgsm_interface_status status;

	enum vgsm_parser_state parser_state;
	ast_cond_t parser_state_change_cond;

	int connect_check_sched_id;

	const struct vgsm_urc *urc;

	struct ast_channel *current_call;

	struct {
		longtime_t timeout;
		char buf[82];
		struct vgsm_response *response;
		int response_error;
	} req;

	struct
	{
		char number[30];
		int ton;
		char subaddress[30];
		int subaddress_type;
		char alpha[32];
		int validity;
	} last_cli;

	char buf[2048];

	struct {
		char vendor[32];
		char model[32];
		char version[32];
		char dob_version[32];
		char serial_number[32];
		char imei[32];
	} module;

	struct {
		char imsi[32];
		int remaining_attempts;
	} sim;

	struct {
		enum vgsm_net_status status;
		char operator_id[6];
		int rxqual;
		int ta;
		int bsic;

		struct {
			int id;
			int lac;
			int arfcn;
			int pwr;
		} cells[16];

		int ncells;
	} net;

};

struct vgsm_state
{
	ast_mutex_t lock;

	struct list_head ifs;
	struct list_head op_list;

	int usecnt;

	int debug;
	

	struct vgsm_interface default_intf;
} vgsm = {
	.usecnt = 0,
#ifdef DEBUG_DEFAULTS
	.debug = TRUE,
#else
	.debug = FALSE,
#endif

	.default_intf = {
		.context = "vgsm",
		.pin = "",
		.min_level = -100,
		.rx_gain = 0,
		.tx_gain = 0,
		.set_clock = 0,
		.operator_selection = VGSM_OPSEL_AUTOMATIC,
		.operator_id = "",
	}
};

#ifdef DEBUG_CODE
#define vgsm_debug(format, arg...)			\
	if (vgsm.debug)					\
		ast_log(LOG_NOTICE,			\
			format,				\
			## arg)
#else
#define vgsm_debug(format, arg...)			\
	do {} while(0);
#endif

#if defined(DEBUG_CODE) && 0
#define vgsm_debug_verb(format, arg...)			\
	if (vgsm.debug)					\
		ast_verbose(VERBOSE_PREFIX_1,		\
			format,				\
			## arg)
#else
#define vgsm_debug_verb(format, arg...)			\
	do {} while(0);
#endif

static int do_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return 0;
}

static int do_no_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return 0;
}

static const char *vgsm_intf_status_to_text(enum vgsm_interface_status status)
{
	switch(status) {
	case VGSM_INT_STATUS_UNINITIALIZED:
		return "UNINITIALIZED";
	case VGSM_INT_STATUS_INITIALIZING:
		return "INITIALIZING";
	case VGSM_INT_STATUS_READY:
		return "READY";
	case VGSM_INT_STATUS_INCALL:
		return "INCALL";
	case VGSM_INT_STATUS_NOT_READY:
		return "NOT_READY";
	case VGSM_INT_STATUS_LOCKED_DOWN:
		return "LOCKED_DOWN";
	case VGSM_INT_STATUS_FAILED:
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

struct vgsm_operator_info *vgsm_search_operator(const char *id)
{
	struct vgsm_operator_info *op_info;

	list_for_each_entry(op_info, &vgsm.op_list, node) {
		if (!strcmp(op_info->id, id))
			return op_info;
	}

	return NULL;
}

static int do_show_vgsm_interfaces(int fd, int argc, char *argv[])
{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		ast_cli(fd, "\n------ Interface '%s' ---------\n", intf->name);

		ast_cli(fd,
			"  Device : %s\n"
			"  Context: %s\n"
			"  RX-gain: %d\n"
			"  TX-gain: %d\n"
			"  Set clock: %s\n"
			"\n",
			intf->device_filename,
			intf->context,
			intf->rx_gain,
			intf->tx_gain,
			intf->set_clock ? "YES" : "NO");

		ast_cli(fd,
			"Module:\n"
			"  Status: %s\n"
			"  Model: %s %s\n"
			"  Version: %s\n"
			"  DOB Version: %s\n"
			"  Serial#: %s\n"
			"  IMEI: %s\n"
			"\n",
			vgsm_intf_status_to_text(intf->status),
			intf->module.vendor,
			intf->module.model,
			intf->module.version,
			intf->module.dob_version,
			intf->module.serial_number,
			intf->module.imei);

		ast_cli(fd,
			"SIM:\n"
			"  IMSI: %s\n"
			"  PIN remaining attempts: %d\n"
			"\n",
			intf->sim.imsi,
			intf->sim.remaining_attempts);

		ast_cli(fd,
			"Network: \n"
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
			"  Status: %s\n",
			vgsm_net_status_to_text(intf->net.status));

		if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	            intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
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
				intf->net.ncells);

			int i;
			for (i=0; i<intf->net.ncells; i++) {
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
	} else if (!strcasecmp(var->name, "min_level")) {
		intf->min_level = atoi(var->value);
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

	dst->min_level = src->min_level;
	dst->rx_gain = src->rx_gain;
	dst->tx_gain = src->tx_gain;
	dst->set_clock = src->set_clock;
	dst->operator_selection = src->operator_selection;

	strncpy(dst->operator_id, src->operator_id,
		sizeof(dst->operator_id));
}

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
			intf = malloc(sizeof(*intf));
			memset(intf, 0, sizeof(*intf));

			ast_mutex_init(&intf->lock);
			ast_cond_init(&intf->parser_state_change_cond, NULL);

			intf->status = VGSM_INT_STATUS_UNINITIALIZED;
			intf->parser_state = VGSM_PS_BITBUCKET;

			intf->fd = -1;
			intf->connect_check_sched_id = -1;

			strncpy(intf->name, cat, sizeof(intf->name));
			vgsm_copy_interface_config(intf, &vgsm.default_intf);

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

static int do_vgsm_reload(int fd, int argc, char *argv[])
{
	vgsm_reload_config();

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

static struct ast_cli_entry no_debug_vgsm_generic =
{
	{ "no", "debug", "vgsm", "generic", NULL },
	do_no_debug_vgsm_generic,
	"Disables generic vGSM debugging",
	NULL,
	NULL
};

static char show_vgsm_interfaces_help[] =
	"Usage: vgsm show interfaces\n"
	"	Displays informations on vGSM interfaces\n";

static struct ast_cli_entry show_vgsm_interfaces =
{
	{ "show", "vgsm", "interfaces", NULL },
	do_show_vgsm_interfaces,
	"Displays vGSM interface information",
	show_vgsm_interfaces_help,
	NULL
};

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


static int vgsm_send_request(
	struct vgsm_interface *intf,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

static struct vgsm_response *vgsm_read_response(
	struct vgsm_interface *intf);

static int vgsm_expect_ok(struct vgsm_interface *intf);

static struct vgsm_response_line *vgsm_response_first_line(
	struct vgsm_response *resp);
static struct vgsm_response_line *vgsm_response_last_line(
	struct vgsm_response *resp);

static void vgsm_respone_get(struct vgsm_response *resp)
{
	resp->refcnt++;
}

static void vgsm_response_put(struct vgsm_response *resp)
{
	resp->refcnt--;

	if (!resp->refcnt) {
		struct vgsm_response_line *line, *n;

		list_for_each_entry_safe(line, n, &resp->lines, node) {
			list_del(&line->node);
			free(line);
		}

		free(resp);
	}
}


static int vgsm_connect_check(void *data)
{
	struct vgsm_interface *intf = data;
	struct vgsm_response *resp;

	if (!intf->current_call) {
		ast_log(LOG_ERROR,
			"Unexpected connect_check with no current_call\n");
		return -1;
	}

	vgsm_send_request(intf, 500, "AT+CPAS");
	resp = vgsm_read_response(intf);

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
	else if (status == 4)
		ast_queue_control(intf->current_call, AST_CONTROL_ANSWER);
	else {
		intf->connect_check_sched_id =
			ast_sched_add(sched, 1000, vgsm_connect_check, intf);
		pthread_kill(vgsm_comm_thread, SIGURG);
	}

	return 0;
}

static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	//struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
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

	int found = FALSE;
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcmp(intf->name, intf_name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
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
	if (intf->status != VGSM_INT_STATUS_READY) {
		ast_mutex_unlock(&intf->lock);
		err = -1;
		goto err_int_not_ready;
	}

	intf->status = VGSM_INT_STATUS_INCALL;
	intf->current_call = ast_chan;

	ast_mutex_unlock(&intf->lock);

	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", intf->name, 1);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	// 'timeout' instead of 20s ?
	vgsm_send_request(intf, 20 * 1000, "ATD%s;", number);
	if (vgsm_expect_ok(intf)) {
		err = -1;
		goto err_atd_failed;
	}

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);
	ast_setstate(ast_chan, AST_STATE_UP);

	intf->connect_check_sched_id =
		ast_sched_add(sched, 1000, vgsm_connect_check, intf);
	pthread_kill(vgsm_comm_thread, SIGURG);

	return 0;

err_atd_failed:
err_int_not_ready:
err_channel_not_down:
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

	vgsm_send_request(intf, 500, "ATA");
	if (vgsm_expect_ok(intf))
		return -1;

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

	vgsm_chan->channel_fd = -1;

	return vgsm_chan;
}

static int vgsm_hangup(struct ast_channel *ast_chan)
{
	vgsm_debug("vgsm_hangup %s\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_interface *intf = vgsm_chan->intf;

	if (intf) {
	//	ast_mutex_lock(&intf->lock);
		if (intf->connect_check_sched_id >= 0)
	//		ast_sched_del(sched, intf->connect_check_sched_id);
		ast_mutex_unlock(&intf->lock);

		vgsm_send_request(intf, 500, "ATH");
		vgsm_expect_ok(intf);

		intf->status = VGSM_INT_STATUS_READY;
		intf->current_call = NULL;
	}

	ast_mutex_lock(&ast_chan->lock);

	close(ast_chan->fds[0]);

	if (vgsm_chan) {
		if (vgsm_chan->channel_fd >= 0) {
			if (ioctl(vgsm_chan->channel_fd,
					VISDN_IOC_DISCONNECT_PATH, NULL) < 0) {
				ast_log(LOG_ERROR,
					"ioctl(VISDN_IOC_DISCONNECT): %s\n",
					strerror(errno));
			}

			if (close(vgsm_chan->channel_fd) < 0) {
				ast_log(LOG_ERROR,
					"close(vgsm_chan->channel_fd): %s\n",
					strerror(errno));
			}

			vgsm_chan->channel_fd = -1;
		}

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

	if (vgsm_chan->channel_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;

		return &f;
	}

	int nread = read(vgsm_chan->channel_fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return &f;
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "R %.3f %d\n",
	t/1000000.0,
	vgsm_chan->channel_fd);
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

	if (vgsm_chan->channel_fd < 0) {
//		ast_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

#if 0
ast_verbose(VERBOSE_PREFIX_3 "W %d %02x%02x%02x%02x%02x%02x%02x%02x %d\n", vgsm_chan->channel_fd,
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

	write(vgsm_chan->channel_fd, frame->data, frame->datalen);

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

	ast_mutex_lock(&usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&usecnt_lock);
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
		if (!strcmp(intf->name, name)) {

			break;
		}
	}
}

/*
static void vgsm_connect_channel(
	struct q931_channel *channel)
{
	FUNC_DEBUG();

	assert(channel->call);
	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	assert(ast_chan->pvt);
	assert(ast_chan->tech_pvt);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	char path[100], dest[100];
	snprintf(path, sizeof(path),
		"/sys/class/net/%s/vgsm_channel/connected/../B%d",
		channel->intf->name,
		channel->id+1);

	memset(dest, 0, sizeof(dest));
	if (readlink(path, dest, sizeof(dest) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		goto err_readlink;
	}

	char *chanid = strrchr(dest, '/');
	if (!chanid || !strlen(chanid + 1)) {
		ast_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest);
		goto err_invalid_chanid;
	}

	strncpy(vgsm_chan->vgsm_chanid, chanid + 1,
		sizeof(vgsm_chan->vgsm_chanid));

	if (vgsm_chan->is_voice) {
		vgsm_debug("Connecting streamport to chan %s\n",
				vgsm_chan->vgsm_chanid);

		vgsm_chan->channel_fd = open("/dev/vgsm/streamport", O_RDWR);
		if (vgsm_chan->channel_fd < 0) {
			ast_log(LOG_ERROR,
				"Cannot open streamport: %s\n",
				strerror(errno));
			goto err_open;
		}

		struct vgsm_connect vc;
		strcpy(vc.src_chanid, "");
		snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s",
			vgsm_chan->vgsm_chanid);
		vc.flags = 0;

		if (ioctl(vgsm_chan->channel_fd, VGSM_IOC_CONNECT,
		    (caddr_t) &vc) < 0) {
			ast_log(LOG_ERROR,
				"ioctl(VGSM_CONNECT): %s\n",
				strerror(errno));
			goto err_ioctl;
		}
	}

	ast_mutex_unlock(&ast_chan->lock);

	return;

err_ioctl:
err_open:
err_invalid_chanid:
err_readlink:

	ast_mutex_unlock(&ast_chan->lock);
}

static void vgsm_q931_disconnect_channel(
	struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan)
		return;

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	ast_mutex_lock(&ast_chan->lock);

	if (vgsm_chan->channel_fd >= 0) {
		if (ioctl(vgsm_chan->channel_fd,
				VGSM_IOC_DISCONNECT, NULL) < 0) {
			ast_log(LOG_ERROR,
				"ioctl(VGSM_IOC_DISCONNECT): %s\n",
				strerror(errno));
		}

		if (close(vgsm_chan->channel_fd) < 0) {
			ast_log(LOG_ERROR,
				"close(vgsm_chan->channel_fd): %s\n",
				strerror(errno));
		}

		vgsm_chan->channel_fd = -1;
	}

	ast_mutex_unlock(&ast_chan->lock);
}
*/






















static char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;
	int len = 0;

	while(*c) {
		switch(*c) {
		case '\r':
			len += snprintf(buf + len, bufsize - len, "<cr>");
		break;
		case '\n':
			len += snprintf(buf + len, bufsize - len, "<lf>");
		break;

		default:
			if (isprint(*c)) {
				len += snprintf(buf + len,
						bufsize - len, "%c", *c);
			} else
				len += snprintf(buf + len,
						bufsize - len, ".");
		}

		c++;
	}

	return buf;
}

static const char *vgsm_parser_state(
	enum vgsm_parser_state state)
{
	switch(state) {
	case VGSM_PS_BITBUCKET:
		return "BITBUCKET";
	case VGSM_PS_IDLE:
		return "IDLE";
	case VGSM_PS_RECOVERING:
		return "RECOVERING";
	case VGSM_PS_AWAITING_RESPONSE:
		return "AWAITING_RESPONSE";
	case VGSM_PS_READING_RESPONSE:
		return "READING_RESPONSE";
	case VGSM_PS_RESPONSE_READY:
		return "RESPONSE_READY";
	case VGSM_PS_READING_URC:
		return "READING_URC";
	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		return "AWAITING_RESPONSE_READING_URC";
	case VGSM_PS_RESPONSE_READY_READING_URC:
		return "RESPONSE_READY_READING_URC";
	}

	return "*UNKNOWN*";
}

static void vgsm_parser_change_state(
	struct vgsm_interface *intf,
	enum vgsm_parser_state newstate)
{
	vgsm_debug_verb("State change from %s to %s\n",
		vgsm_parser_state(intf->parser_state),
		vgsm_parser_state(newstate));

	intf->parser_state = newstate;

	ast_cond_broadcast(&intf->parser_state_change_cond);
}

static int vgsm_module_recover(struct vgsm_interface *intf)
{
	vgsm_parser_change_state(intf, VGSM_PS_RECOVERING);

	sleep(1);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	sleep(1);

	const char *cmd = "AT Z0 &F E1 V1 Q0 &K0\r";
	if (write(intf->fd, cmd, strlen(cmd)) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	ast_mutex_lock(&intf->lock);
	while(intf->parser_state != VGSM_PS_IDLE)
		ast_cond_wait(&intf->parser_state_change_cond, &intf->lock);
	ast_mutex_unlock(&intf->lock);

	return 0;
}

static struct vgsm_response_line *vgsm_response_first_line(
	struct vgsm_response *resp)
{
	return list_entry(resp->lines.next, struct vgsm_response_line, node);
}

static struct vgsm_response_line *vgsm_response_last_line(
	struct vgsm_response *resp)
{
	return list_entry(resp->lines.prev, struct vgsm_response_line, node);
}

static struct vgsm_response *vgsm_response_alloc()
{
	struct vgsm_response *resp;

	resp = malloc(sizeof(*resp));
	if (!resp)
		return NULL;

	memset(resp, 0, sizeof(*resp));

	resp->refcnt = 1;

	INIT_LIST_HEAD(&resp->lines);

	return resp;
}

static struct vgsm_response *vgsm_read_response(
	struct vgsm_interface *intf)
{
	struct vgsm_response *resp;

	ast_mutex_lock(&intf->lock);
	while(intf->parser_state != VGSM_PS_RESPONSE_READY &&
	      intf->parser_state != VGSM_PS_RESPONSE_READY_READING_URC)
		ast_cond_wait(&intf->parser_state_change_cond, &intf->lock);

	vgsm_parser_change_state(intf, VGSM_PS_IDLE);
	resp = intf->req.response;
	intf->req.response = NULL;

	ast_mutex_unlock(&intf->lock);

	return resp;
}

static int vgsm_expect_ok(struct vgsm_interface *intf)
{
	ast_mutex_lock(&intf->lock);
	while(intf->parser_state != VGSM_PS_RESPONSE_READY &&
	      intf->parser_state != VGSM_PS_RESPONSE_READY_READING_URC)
		ast_cond_wait(&intf->parser_state_change_cond, &intf->lock);

	int error_code = intf->req.response_error;

	vgsm_parser_change_state(intf, VGSM_PS_IDLE);
	vgsm_response_put(intf->req.response);
	intf->req.response = NULL;

	ast_mutex_unlock(&intf->lock);

	return error_code;
}

static longtime_t longtime_now()
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

static int vgsm_send_request(
	struct vgsm_interface *intf,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	char buf[82];

	va_start(ap, fmt);

	if (vsnprintf(buf, sizeof(buf), fmt, ap) >= sizeof(buf) - 1)
		return -1;

	ast_mutex_lock(&intf->lock);
	while(intf->parser_state != VGSM_PS_IDLE)
		ast_cond_wait(&intf->parser_state_change_cond, &intf->lock);

	strncpy(intf->req.buf, buf, sizeof(intf->req.buf));
	vgsm_parser_change_state(intf, VGSM_PS_AWAITING_RESPONSE);
	intf->req.response = vgsm_response_alloc();
	intf->req.timeout = longtime_now() + timeout * 1000;
	ast_mutex_unlock(&intf->lock);

	strcat(buf, "\r");

	char tmpstr[80];
	vgsm_debug_verb("TX: '%s'\n",
		unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	pthread_kill(vgsm_comm_thread, SIGURG);

	return write(intf->fd, buf, strlen(buf));
}

static void vgsm_retransmit_request(struct vgsm_interface *intf)
{
	char buf[82];
	strncpy(buf, intf->req.buf, sizeof(buf));
	strcat(buf, "\r");

	char tmpstr[80];
	vgsm_debug_verb("TX: '%s'\n",
		unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	if (write(intf->fd, buf, strlen(buf)) < 0)
		ast_log(LOG_WARNING,
			"Cannot write to module: %s\n",
			strerror(errno));
}

static void handle_response_line(
	struct vgsm_interface *intf,
	const char *line)
{
	// Handle response
	char tmpstr[80];
	vgsm_debug_verb("RX: '%s' (crlf)\n",
		unprintable_escape(line, tmpstr, sizeof(tmpstr)));

	struct vgsm_response_line *resp_line;
	resp_line = malloc(sizeof(struct vgsm_response_line) +
			 strlen(line) + 1);

	strcpy(resp_line->text, line);

	list_add_tail(&resp_line->node, &intf->req.response->lines);

	if (!strcmp(line, "OK")) {
		intf->req.response_error = 0;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "CONNECT")) {
		intf->req.response_error = 1;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO CARRIER")) {
		intf->req.response_error = 3;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "ERROR")) {
		intf->req.response_error = 4;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO DIALTONE")) {
		intf->req.response_error = 6;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "BUSY")) {
		intf->req.response_error = 7;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO ANSWER")) {
		intf->req.response_error = 8;
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	} else if (strstr(line, "+CME ERROR: ") == line) {
		intf->req.response_error = atoi(line + strlen("+CME ERROR: "));
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	}
}

static void handle_unsolicited_busy(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	if (intf->current_call)
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
}

static void handle_unsolicited_no_dialtone(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	ast_log(LOG_WARNING, "'NO DIALTONE' not yet handled\n");
}

static void handle_unsolicited_no_carrier(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	if (intf->current_call)
		ast_softhangup(intf->current_call, AST_SOFTHANGUP_DEV);
}

static void handle_unsolicited_ring(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	ast_log(LOG_NOTICE, "Unexpected RING\n");
}

static void handle_unsolicited_cring(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	if (intf->status != VGSM_INT_STATUS_READY) {
		ast_log(LOG_NOTICE, "Rejecting RING on not ready interface\n");
		vgsm_send_request(intf, 500, "ATH");
		vgsm_expect_ok(intf);

		return;
	}

	if (intf->current_call) {
		ast_log(LOG_NOTICE, "Updating timeout\n");
		ast_channel_setwhentohangup(intf->current_call, 6);
		return;
	}

	if (strcmp(pars, "VOICE")) {
		ast_log(LOG_NOTICE, "Not a voice call, rejecting\n");
		vgsm_send_request(intf, 500, "ATH");
		vgsm_expect_ok(intf);

		return;
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

	ast_mutex_lock(&usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&usecnt_lock);
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

		vgsm_send_request(intf, 500, "ATH");
	}

	return;

err_pbx_start:
	ast_hangup(ast_chan);
err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:

	return;
}

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

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

		if (!*p || *p == ',') {
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
	const char *pars)
{
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field)))
		goto err_no_mode;

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
	struct vgsm_response *resp;

	vgsm_send_request(intf, 5 * 1000, "AT+COPS?");
	resp = vgsm_read_response(intf);
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


static void handle_unsolicited_creg(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	if (strlen(pars) > 7)
		vgsm_update_intf_by_creg(intf, pars + 7);

	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_log(LOG_NOTICE,
			"Module '%s' registration %s (LAC=0x%04x, CI=0x%04x)\n",
			intf->name,
			vgsm_net_status_to_text(intf->net.status),
			intf->net.cells[0].lac,
			intf->net.cells[0].id);
	} else {
		ast_log(LOG_NOTICE,
			"Module '%s' registration %s\n",
			intf->name,
			vgsm_net_status_to_text(intf->net.status));
	}

	vgsm_update_cops(intf);
}

static void handle_unsolicited_cusd(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cccm(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_ccwa(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_clip(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	const char *pars_ptr = pars;
	char field[32];

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

	return;

err_parse_cid:

	return;
}

static void handle_unsolicited_cssi(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cssu(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_alarm(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cgreg(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cmti(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cmt(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_cbm(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	switch(intf->parser_state) {
	case VGSM_PS_READING_URC:
		ast_log(LOG_NOTICE, "CBM: %s\n", line);
		vgsm_parser_change_state(intf, VGSM_PS_IDLE);
	break;

	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		ast_log(LOG_NOTICE, "CBM: %s\n", line);
		vgsm_parser_change_state(intf, VGSM_PS_AWAITING_RESPONSE);
	break;

	case VGSM_PS_RESPONSE_READY_READING_URC:
		ast_log(LOG_NOTICE, "CBM: %s\n", line);
		vgsm_parser_change_state(intf, VGSM_PS_RESPONSE_READY);
	break;

	case VGSM_PS_IDLE:
		intf->urc = urc;
		vgsm_parser_change_state(intf, VGSM_PS_READING_URC);
	break;

	case VGSM_PS_RESPONSE_READY:
		intf->urc = urc;
		vgsm_parser_change_state(intf,
			VGSM_PS_RESPONSE_READY_READING_URC);
	break;

	case VGSM_PS_AWAITING_RESPONSE:
		intf->urc = urc;
		vgsm_parser_change_state(intf,
			VGSM_PS_AWAITING_RESPONSE_READING_URC);
	break;

	case VGSM_PS_BITBUCKET:
	case VGSM_PS_RECOVERING:
	case VGSM_PS_READING_RESPONSE:
		ast_log(LOG_ERROR,
			"Unexpected handle_unsolicited_cbm in state %d\n",
			intf->parser_state);
	break;
	}
	
}

static void handle_unsolicited_cds(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
}

static void handle_unsolicited_qss(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	ast_log(LOG_NOTICE, "QSS\n");
}

static void handle_unsolicited_jdr(
	const struct vgsm_urc *urc,
	struct vgsm_interface *intf,
	const char *line,
	const char *pars)
{
	ast_log(LOG_WARNING, "Jammin'... jammin' in the name of the Lord.\n");
}

static struct vgsm_urc urcs[] =
{
	{ "BUSY", handle_unsolicited_busy },
	{ "NO CARRIER", handle_unsolicited_no_carrier },
	{ "NO DIALTONE", handle_unsolicited_no_dialtone },
	{ "BUSY", handle_unsolicited_busy },
	{ "RING: ", handle_unsolicited_ring },
	{ "+CRING: ", handle_unsolicited_cring },
	{ "+CREG: ", handle_unsolicited_creg },
	{ "+CUSD: ", handle_unsolicited_cusd },
	{ "+CCWA: ", handle_unsolicited_ccwa },
	{ "+CLIP: ", handle_unsolicited_clip },
	{ "+CCCM: ", handle_unsolicited_cccm },
	{ "+CSSI: ", handle_unsolicited_cssi },
	{ "+CSSU: ", handle_unsolicited_cssu },
	{ "+ALARM: ", handle_unsolicited_alarm },
	{ "+CGREG: ", handle_unsolicited_cgreg },
	{ "+CMTI: ", handle_unsolicited_cmti },
	{ "+CMT: ", handle_unsolicited_cmt },
	{ "+CBM: ", handle_unsolicited_cbm },
	{ "+CDS: ", handle_unsolicited_cds },
	{ "#QSS: ", handle_unsolicited_qss },
	{ "#JDR: ", handle_unsolicited_jdr },
};

static void handle_unsolicited_line(
	struct vgsm_interface *intf,
	const char *line)
{
	int i;
	for (i=0; i<ARRAY_SIZE(urcs); i++) {
		if (strstr(line, urcs[i].code) == line) {
			urcs[i].handler(&urcs[i], intf, line,
				line + strlen(urcs[i].code));

			return;
		}
	}

	ast_log(LOG_WARNING,
		"Unsupported/unexpected unsolicited"
		" code '%s'\n",
		line);
}

static int handle_crlf_msg_crlf(struct vgsm_interface *intf)
{
	char *begin, *end;

	begin = intf->buf;

	if (*(begin + 1) == '\0')
		return 0;

	if (*(begin + 1) == '\r')
		return 1;

	if (*(begin + 1) != '\n') {
		ast_log(LOG_WARNING, "Unexpected char 0x%02x after <cr>\n",
			*(begin + 1));

		return 1;
	}

	end = strchr(begin + 2, '\n');
	if (!end)
		return 0;

	if (*(end - 1) != '\r') {
		ast_log(LOG_WARNING, "Unexpected char 0x%02x before <lf>\n",
			*(end - 1));

		return end - begin + 1;
	}

	*(end - 1) = '\0';

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(begin + 2, tmpstr, sizeof(tmpstr)));

	switch(intf->parser_state) {
	case VGSM_PS_READING_RESPONSE:
	case VGSM_PS_AWAITING_RESPONSE:
		handle_response_line(intf, begin + 2);
	break;

	case VGSM_PS_IDLE:
	case VGSM_PS_RESPONSE_READY:
	case VGSM_PS_READING_URC:
	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
	case VGSM_PS_RESPONSE_READY_READING_URC:
		handle_unsolicited_line(intf, begin + 2);
	break;

	case VGSM_PS_BITBUCKET:
	case VGSM_PS_RECOVERING:
		assert(1);
	break;
	}

	return end - intf->buf + 1;
}

static int handle_msg_cr(struct vgsm_interface *intf)
{
	char *firstcr;
	firstcr = strchr(intf->buf, '\r');

	*firstcr = '\0';

	if (intf->parser_state != VGSM_PS_AWAITING_RESPONSE)
		return firstcr - intf->buf + 1;

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(intf->buf, tmpstr, sizeof(tmpstr)));

	if (!strncmp(intf->buf, intf->req.buf, strlen(intf->req.buf))) {
		
		vgsm_parser_change_state(intf, VGSM_PS_READING_RESPONSE);
	} else {
		char *dropped = malloc(firstcr - intf->buf + 1);

		memcpy(dropped, intf->buf, firstcr - intf->buf);
		dropped[firstcr - intf->buf] = '\0';

		char tmpstr[80];
		ast_log(LOG_WARNING,
			"Dropped spurious bytes '%s' from serial\n",
			unprintable_escape(dropped, tmpstr, sizeof(tmpstr)));

		free(dropped);
	}

	return firstcr - intf->buf + 1;
}

static int handle_msg_crlf(struct vgsm_interface *intf)
{
	char *lf;
	lf = strchr(intf->buf, '\n');

	if (lf == intf->buf) {
		ast_log(LOG_WARNING, "Unexpected <lf>\n");
		return 1;
	}

	*(lf - 1) = '\0';

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(intf->buf, tmpstr, sizeof(tmpstr)));

	intf->urc->handler(intf->urc, intf, intf->buf,
			intf->buf + strlen(intf->urc->code));

	return lf - intf->buf + 1;
}

static int vgsm_receive(struct vgsm_interface *intf)
{
	int buflen = strlen(intf->buf);
	int nread;

	nread = read(intf->fd, intf->buf + buflen,
			sizeof(intf->buf) - buflen - 1);
	if (nread < 0) {
		ast_log(LOG_WARNING, "Error reading from serial: %s\n",
			strerror(errno));

		return -1;
	}

	intf->buf[buflen + nread] = '\0';

	while(1) {
		int nread = 0;

#if 0
char tmpstr[80];
ast_verbose("BUF='%s'\n",
	unprintable_escape(intf->buf, tmpstr, sizeof(tmpstr)));
#endif

		switch(intf->parser_state) {
		case VGSM_PS_BITBUCKET:
			/* Throw away everything */
			nread = strlen(intf->buf);
		break;

		case VGSM_PS_RECOVERING:
			if (strstr(intf->buf, "\r\nOK\r\n")) {
				nread = strlen(intf->buf);

				vgsm_parser_change_state(intf,
					VGSM_PS_IDLE);
			}
		break;

		case VGSM_PS_READING_URC:
		case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		case VGSM_PS_RESPONSE_READY_READING_URC: {
			const char *firstlf = strchr(intf->buf, '\n');
			if (firstlf)
				nread = handle_msg_crlf(intf);
		}
		break;

		case VGSM_PS_IDLE:
		case VGSM_PS_AWAITING_RESPONSE:
		case VGSM_PS_READING_RESPONSE:
		case VGSM_PS_RESPONSE_READY: {
			const char *firstcr;
			firstcr = strchr(intf->buf, '\r');

			if (firstcr > intf->buf)
				nread = handle_msg_cr(intf);
			else if (firstcr == intf->buf)
				nread = handle_crlf_msg_crlf(intf);
		}
		break;
		}

		if (nread)
			memmove(intf->buf, intf->buf + nread,
				strlen(intf->buf + nread) + 1);
		else
			break;
	}

	return 0;
}

static int vgsm_comm_thread_do_stuff()
{
	struct pollfd polls[64];
	struct vgsm_interface *ifs[64];
	int npolls = 0;

	{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		if (intf->fd < 0)
			continue;

		polls[npolls].fd = intf->fd;
		polls[npolls].events = POLLERR | POLLIN;

		ifs[npolls] = intf;

		npolls++;
	}
	}

	for(;;) {
		int i;
		longtime_t now = longtime_now();
		longtime_t timeout = -1;

		for (i=0; i<npolls; i++) {
			ast_mutex_lock(&ifs[i]->lock);
			if (ifs[i]->parser_state != VGSM_PS_IDLE &&
			    ifs[i]->req.timeout - now > 0 &&
			    (ifs[i]->req.timeout - now < timeout ||
			     timeout == -1))
				timeout = ifs[i]->req.timeout - now;
			ast_mutex_unlock(&ifs[i]->lock);
		}

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = timeout / 1000 + 1;

		vgsm_debug_verb("poll timeout = %d\n", timeout_ms);

		int res = poll(polls, npolls, timeout_ms);
		if (res < 0) {
			if (errno == EINTR) {
				/* Force reload of polls */
				return 1;
			}

			ast_log(LOG_WARNING, "Error polling serial: %s\n",
				strerror(errno));

			return 0;
		}

		now = longtime_now();

		for (i=0; i<npolls; i++) {
			ast_mutex_lock(&ifs[i]->lock);
			if (ifs[i]->parser_state == VGSM_PS_AWAITING_RESPONSE &&
			    ifs[i]->req.timeout < now) {
				vgsm_retransmit_request(ifs[i]);
			}
			ast_mutex_unlock(&ifs[i]->lock);

			if (polls[i].revents & POLLIN)
				vgsm_receive(ifs[i]);
		}
	}
}

static int vgsm_input_pin(struct vgsm_interface *intf)
{
	struct vgsm_response *resp;
	struct vgsm_response_line *first_line;
	int res = 0;

	vgsm_send_request(intf, 20 * 1000, "AT+CPIN");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	first_line = vgsm_response_first_line(resp);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

#if 0
		/* Only do ONE attempt automatically, further attempts must
		 * be explicitly required by the user */

		vgsm_send_request(intf, "AT+CPIN=%s", intf->pin);
		if (vgsm_expect_ok(intf, 20 * 1000) < 0) {
			ast_log(LOG_WARNING,
				"SIM PIN refused, aborting module initialization\n");

			return -1;
		}
#else
		res = -1;
#endif
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_log(LOG_WARNING,
			"SIM requires PIN2, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		ast_log(LOG_WARNING,
			"SIM requires PUK, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_log(LOG_WARNING,
			"SIM requires PUK2, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 10")) {
		ast_log(LOG_NOTICE,
			"SIM not present, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 13")) {
		ast_log(LOG_WARNING,
			"SIM defective, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 14")) {
		ast_log(LOG_WARNING,
			"SIM busy, aborting module initialization\n");

		res = -1;
	} else if (!strcmp(first_line->text, "+CME ERROR: 15")) {
		ast_log(LOG_WARNING,
			"Wrong type of SIM, aborting module initialization\n");

		res = -1;
	} else {
		ast_log(LOG_WARNING,
			"Unknown response '%s'"
			", aborting module initialization\n", first_line->text);

		res = -1;
	}

	vgsm_response_put(resp);

	return res;
}

static int vgsm_module_init(struct vgsm_interface *intf)
{
	struct vgsm_response *resp;

	struct vgsm_codec_ctl cctl;

	cctl.parameter = VGSM_CODEC_RXGAIN;
	cctl.value = intf->rx_gain;

	if (ioctl(intf->fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_CODEC_SET) failed: %s\n",
			strerror(errno));

		return -1;
	}

	cctl.parameter = VGSM_CODEC_TXGAIN;
	cctl.value = intf->tx_gain;

	if (ioctl(intf->fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		ast_log(LOG_ERROR, "ioctl(IOC_CODEC_SET) failed: %s\n",
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

	/* Very basic onfiguration before entering PIN */

	/* "AT" is needed before AT+IPR, otherwise it
	 * responds with ERROR */

	vgsm_send_request(intf, 500, "AT");
	if (vgsm_expect_ok(intf))
		return -1;

	vgsm_send_request(intf, 500, "AT+IPR=38400");
	if (vgsm_expect_ok(intf))
		return -1;

	vgsm_send_request(intf, 500, "AT+CMEE=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Be careful not consume all the available attempts */
	vgsm_send_request(intf, 500, "AT#PCT");

	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	intf->sim.remaining_attempts =
		atoi(vgsm_response_first_line(resp)->text + strlen("#PCT: "));

	vgsm_response_put(resp);

	if (intf->sim.remaining_attempts <= 2) {
		ast_log(LOG_WARNING,
			"Module '%s' has only %d available PIN/PUK attempts."
			" Please enter it manually.\n",
			intf->name,
			intf->sim.remaining_attempts);

		return -1;
	}

	if (vgsm_input_pin(intf) < 0)
		return -1;

	/* Configure operator selection */
	switch(intf->operator_selection) {
	case VGSM_OPSEL_AUTOMATIC:
		vgsm_send_request(intf, 500, "AT+COPS=0,2");
	break;

	case VGSM_OPSEL_MANUAL_UNLOCKED:
		vgsm_send_request(intf, 500, "AT+COPS=1,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_FALLBACK:
		vgsm_send_request(intf, 500, "AT+COPS=4,2,%s",
			intf->operator_id);
	break;

	case VGSM_OPSEL_MANUAL_LOCKED:
		vgsm_send_request(intf, 500, "AT+COPS=5,2,%s",
			intf->operator_id);
	break;
	}

	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable unsolicited registration informations */
	vgsm_send_request(intf, 500, "AT+CREG=2");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable unsolicited unstructured supplementary data */
	vgsm_send_request(intf, 500, "AT+CUSD=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable unsolicited GPRS registration status */
	vgsm_send_request(intf, 500, "AT+CGREG=2");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable unsolicited supplementary service notification */
	vgsm_send_request(intf, 500, "AT+CSSN=1,1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Subscribe to all cell broadcast channels */
	vgsm_send_request(intf, 500, "AT+CSCB=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable extended cellular result codes */
	vgsm_send_request(intf, 500, "AT+CRC=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable Calling Line Presentation */
	vgsm_send_request(intf, 500, "AT+CLIP=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable unsolicited advice of charge notifications */
	vgsm_send_request(intf, 500, "AT+CAOC=2");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Sets current time on module */
	if (intf->set_clock) {
		struct tm *tm;
		time_t ct = time(NULL);

		tm = localtime(&ct);

		vgsm_send_request(intf, 500,
			"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03ld\"",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			-(timezone / 3600) + tm->tm_isdst);
		if (vgsm_expect_ok(intf))
			return -1;
	}

	/* Enable unsolicited new message indications */
	vgsm_send_request(intf, 500, "AT+CNMI=2,1,2,1,0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Select handsfree audio path */
	vgsm_send_request(intf, 500, "AT#CAP=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Disable tones */
	vgsm_send_request(intf, 500, "AT#STM=0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Set ringer auto directed to GPIO7 */
	vgsm_send_request(intf, 500, "AT#SRP=3");
	if (vgsm_expect_ok(intf))
		return -1;

// SET #SGPO
	/* Enable unsolicited SIM status reporting */
	vgsm_send_request(intf, 500, "AT#QSS=1");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Disable handsfree echo canceller */
	vgsm_send_request(intf, 500, "AT#SHFEC=0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Set MIC audio gain to +0dB */
	vgsm_send_request(intf, 500, "AT#HFMICG=0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Disable sidetone generation */
	vgsm_send_request(intf, 500, "AT#SHFSD=0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Set GSM1800 band */
	vgsm_send_request(intf, 500, "AT#BND=0");
	if (vgsm_expect_ok(intf))
		return -1;

	/* Enable Jammer detector */
	vgsm_send_request(intf, 500, "AT#JDR=2");
	if (vgsm_expect_ok(intf))
		return -1;

	return 0;
}

static int vgsm_module_update_static_info(
	struct vgsm_interface *intf)
{
	struct vgsm_response *resp;

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+CGMI");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.vendor,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.vendor));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+CGMM");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.model,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.model));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "ATI5");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.dob_version,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.dob_version));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+CGMR");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.version,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.version));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+GSN");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.serial_number,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.serial_number));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+CGSN");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->module.imei,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->module.imei));

	vgsm_response_put(resp);

	/*--------*/
	vgsm_send_request(intf, 5 * 1000, "AT+CIMI");
	resp = vgsm_read_response(intf);
	if (!resp)
		return -1;

	strncpy(intf->sim.imsi,
		vgsm_response_first_line(resp)->text,
		sizeof(intf->sim.imsi));

	vgsm_response_put(resp);

	return 0;
}

static int vgsm_module_update_net_info(
	struct vgsm_interface *intf)
{
	struct vgsm_response *resp;

	vgsm_send_request(intf, 5 * 1000, "AT+CREG?");
	resp = vgsm_read_response(intf);
	if (!resp)
		goto err_creg_read_response;

	const char *line = vgsm_response_first_line(resp)->text;
	const char *field;

	if (strlen(line) > 7)
		vgsm_update_intf_by_creg(intf, line + 7);
	vgsm_response_put(resp);
err_creg_read_response:

	intf->net.ncells = 0;

	int i;
	for (i=0; i<ARRAY_SIZE(intf->net.cells); i++) {
		vgsm_send_request(intf, 5 * 1000, "AT#MONI=%d", i);
		if (vgsm_expect_ok(intf))
			break;

		vgsm_send_request(intf, 5 * 1000, "AT#MONI");
		resp = vgsm_read_response(intf);
		if (!resp)
			break;

		line = vgsm_response_first_line(resp)->text;

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

	return 0;

/*	AT+CSQ
	AT#CSURV
*/
}


static void *vgsm_comm_thread_main(void *data)
{
	while(vgsm_comm_thread_do_stuff());

	return NULL;
}

static void vgsm_module_update_readiness(
	struct vgsm_interface *intf)
{
	if (intf->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    intf->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		intf->status = VGSM_INT_STATUS_READY;
	}
}

static void vgsm_monitor_attempt_initialization()
{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (intf->fd < 0) {
			intf->fd = open(intf->device_filename, O_RDWR);
			if (intf->fd < 0) {
				ast_log(LOG_WARNING,
					"Unable to open '%s': %s\n",
					intf->device_filename,
					strerror(errno));

				intf->status = VGSM_INT_STATUS_FAILED;
				continue;
			}

			pthread_kill(vgsm_comm_thread, SIGURG);
		}

		if (intf->status != VGSM_INT_STATUS_UNINITIALIZED &&
		    intf->status != VGSM_INT_STATUS_FAILED)
			continue;

		intf->status = VGSM_INT_STATUS_INITIALIZING;

		ast_log(LOG_NOTICE, "Initializing module '%s'\n", intf->name);

		if (vgsm_module_recover(intf) < 0) {
			vgsm_parser_change_state(intf, VGSM_PS_BITBUCKET);
			intf->status = VGSM_INT_STATUS_LOCKED_DOWN;
			continue;
		}

		if (vgsm_module_init(intf) < 0) {
			vgsm_parser_change_state(intf, VGSM_PS_BITBUCKET);
			intf->status = VGSM_INT_STATUS_LOCKED_DOWN;
			continue;
		}

		intf->status = VGSM_INT_STATUS_NOT_READY;

		vgsm_module_update_static_info(intf);

		if (vgsm_module_update_net_info(intf) < 0) {
			vgsm_parser_change_state(intf, VGSM_PS_BITBUCKET);
			intf->status = VGSM_INT_STATUS_LOCKED_DOWN;
			continue;
		}

		vgsm_module_update_readiness(intf);

		ast_log(LOG_NOTICE, "Module '%s' successfully initialized\n",
			intf->name);
	}
}

#define MAINT_SLEEP 60

static void *vgsm_monitor_thread_main(void *data)
{
	sleep(2);

	int last_maint = 0;

	for(;;) {
		if (time(NULL) >= last_maint + MAINT_SLEEP) {
			vgsm_monitor_attempt_initialization();

			struct vgsm_interface *intf;
			list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
				if (intf->status == VGSM_INT_STATUS_READY)
					vgsm_module_update_net_info(intf);
			}

			last_maint = time(NULL);
		}

		int res;
		res = ast_sched_wait(sched);
		if (res > 0 && res < MAINT_SLEEP * 1000)
			usleep(res * 1000);
		else
			sleep(MAINT_SLEEP);

		res = ast_sched_runq(sched);
	}

	return NULL;
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	ast_mutex_init(&vgsm.lock);

	INIT_LIST_HEAD(&vgsm.ifs);
	INIT_LIST_HEAD(&vgsm.op_list);

	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		return -1;
	}

	vgsm_reload_config();

	if (ast_channel_register(&vgsm_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VGSM_CHAN_TYPE);
		return -1;
	}

	ast_cli_register(&debug_vgsm_generic);
	ast_cli_register(&no_debug_vgsm_generic);
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&show_vgsm_interfaces);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&vgsm_comm_thread, &attr,
					vgsm_comm_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start communication thread.\n");
		return -1;
	}

	if (ast_pthread_create(&vgsm_monitor_thread, &attr,
					vgsm_monitor_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start maintainance thread.\n");
		return -1;
	}

	return res;
}

int unload_module(void)
{
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	if (sched)
		sched_context_destroy(sched);

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
	ast_mutex_lock(&usecnt_lock);
	res = vgsm.usecnt;
	ast_mutex_unlock(&usecnt_lock);
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

/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * vISDN Linux Telephony Interface driver
 * 
 * Copyright 2004,2005 Daniele "Vihai" Orlandi <daniele@orlandi.com>
 * Copyright 2004 Lele Forzani <lele@windmill.it>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <linux/rtc.h>

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>

#include <softport.h>
#include <lapd.h>
#include <libq931/q931.h>

#include <visdn.h>

//#include "echo.h"

#define FRAME_SIZE 160

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

// static char context[AST_MAX_EXTENSION] = "default";

static pthread_t visdn_q931_thread = AST_PTHREADT_NULL;

#define VISDN_DESCRIPTION "VISDN Channel For Asterisk"
#define VISDN_CHAN_TYPE "VISDN"
#define VISDN_CONFIG_FILE "visdn.conf"

struct visdn_chan {
	struct ast_channel *ast_chan;
	struct q931_call *q931_call;

	int channel_fd;

	char calling_number[21];
	char called_number[21];
	int sending_complete;

//	echo_can_state_t *ec;
};

enum poll_info_type
{
	POLL_INFO_TYPE_INTERFACE,
	POLL_INFO_TYPE_DLC,
	POLL_INFO_TYPE_NETLINK,
};

struct poll_info
{
	enum poll_info_type type;
	union
	{
		struct q931_interface *interface;
		struct q931_dlc *dlc;
	};
};

AST_MUTEX_DEFINE_STATIC(q931_lock);

enum visdn_type_of_number
{
	VISDN_TYPE_OF_NUMBER_UNKNOWN,
	VISDN_TYPE_OF_NUMBER_INTERNATIONAL,
	VISDN_TYPE_OF_NUMBER_NATIONAL,
	VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC,
	VISDN_TYPE_OF_NUMBER_SUBSCRIBER,
	VISDN_TYPE_OF_NUMBER_ABBREVIATED,
};

struct visdn_interface
{
	char name[IFNAMSIZ];

	int configured;

	enum q931_interface_network_role network_role;
	enum visdn_type_of_number type_of_number;
	enum visdn_type_of_number local_type_of_number;
	int tones_option;
	char context[AST_MAX_EXTENSION];
	char default_inbound_caller_id[128];
	int force_inbound_caller_id;
	int overlap_sending;
	int overlap_receiving;

	struct q931_interface *q931_intf;
};

struct visdn_state
{
	struct q931_lib *libq931;

	int have_to_exit;

	struct visdn_interface ifs[100];
	int nifs;

	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls;

	int usecnt;
	int timer_fd;
	int control_fd;
	int netlink_socket;

	int debug;
	int debug_q931;
	int debug_q921;

	struct visdn_interface default_intf;
} visdn = {
	.usecnt = 0,
	.timer_fd = -1,
	.control_fd = -1,
	.debug = TRUE,
	.debug_q921 = FALSE,
	.debug_q931 = TRUE,

	.default_intf = {
		.network_role = Q931_INTF_NET_PRIVATE,
		.type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN,
		.local_type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN,
		.tones_option = TRUE,
		.context = "visdn",
		.default_inbound_caller_id = "",
		.force_inbound_caller_id = FALSE,
		.overlap_sending = TRUE,
		.overlap_receiving = FALSE,
	}
};

static void visdn_set_socket_debug(int on)
{
	int i;

	for(i = 0; i < visdn.nifs; i++) {
		if (!visdn.ifs[i].q931_intf)
			continue;

		if (visdn.ifs[i].q931_intf->role == LAPD_ROLE_NT) {
			setsockopt(visdn.ifs[i].q931_intf->master_socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		} else {
			setsockopt(visdn.ifs[i].q931_intf->dlc.socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc, &visdn.ifs[i].q931_intf->dlcs, intf_node) {
			setsockopt(dlc->socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}
	}

}

static int do_debug_visdn_generic(int fd, int argc, char *argv[])
{
	visdn.debug = TRUE;

	ast_cli(fd, "vISDN debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_generic(int fd, int argc, char *argv[])
{
	visdn.debug = FALSE;

	ast_cli(fd, "vISDN debugging disabled\n");

	return 0;
}

static int do_debug_visdn_q921(int fd, int argc, char *argv[])
{
	// Enable debugging on new DLCs FIXME TODO

	visdn.debug_q921 = TRUE;

	visdn_set_socket_debug(1);

	ast_cli(fd, "vISDN q.921 debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_q921(int fd, int argc, char *argv[])
{
	// Disable debugging on new DLCs FIXME TODO

	visdn.debug_q921 = FALSE;

	visdn_set_socket_debug(0);

	ast_cli(fd, "vISDN q.921 debugging disabled\n");

	return 0;
}

static int do_debug_visdn_q931(int fd, int argc, char *argv[])
{
	// Implement locking

	visdn.debug_q931 = TRUE;

	ast_cli(fd, "vISDN q.931 debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_q931(int fd, int argc, char *argv[])
{
	// Implement locking

	visdn.debug_q931 = FALSE;

	ast_cli(fd, "vISDN q.931 debugging disabled\n");

	return 0;
}

static const char *visdn_interface_network_role_to_string(
	enum q931_interface_network_role value)
{
	switch(value) {
	case Q931_INTF_NET_USER:
		return "User";
	case Q931_INTF_NET_PRIVATE:
		return "Private Network";
	case Q931_INTF_NET_LOCAL:
		return "Local Network";
	case Q931_INTF_NET_TRANSIT:
		return "Transit Network";
	case Q931_INTF_NET_INTERNATIONAL:
		return "International Network";
	}
}

static const char *visdn_type_of_number_to_string(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return "unknown";
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return "international";
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return "national";
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return "network specific";
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return "subscriber";
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return "private";
	default:
		return "INVALID!";
	}
}

static int do_show_visdn_interfaces(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&q931_lock);

	int i;
	for (i=0; i<visdn.nifs; i++) {
		struct visdn_interface *intf = &visdn.ifs[i];

		ast_cli(fd, "\n------ Interface %s ---------\n", intf->name);

		ast_cli(fd, "Role                      : %s\n",
				intf->q931_intf ?
					(intf->q931_intf->role == LAPD_ROLE_NT ?
						"NT" : "TE") :
					"UNUSED!");

		ast_cli(fd, 
			"Network role              : %s\n"
			"Type of number            : %s\n"
			"Local type of number      : %s\n"
			"Tones option              : %s\n"
			"Context                   : %s\n"
			"Default inbound caller ID : %s\n"
			"Force inbound caller ID   : %s\n"
			"Overlap Sending           : %s\n"
			"Overlap Receiving         : %s\n",
			visdn_interface_network_role_to_string(
				intf->network_role),
			visdn_type_of_number_to_string(
				intf->type_of_number),
			visdn_type_of_number_to_string(
				intf->local_type_of_number),
			intf->tones_option ? "Yes" : "No",
			intf->context,
			intf->default_inbound_caller_id,
			intf->force_inbound_caller_id ? "Yes" : "No",
			intf->overlap_sending ? "Yes" : "No",
			intf->overlap_receiving ? "Yes" : "No");

		if (intf->q931_intf) {
			ast_cli(fd,
				"DLCs                      : ");

			struct q931_dlc *dlc;
			list_for_each_entry(dlc, &intf->q931_intf->dlcs,
					intf_node) {
				ast_cli(fd, "%d ", dlc->tei);
			}

			ast_cli(fd, "\n");
		}
	}

	ast_mutex_unlock(&q931_lock);

	return 0;
}

static enum visdn_type_of_number visdn_string_to_type_of_number(const char *str)
{
	 enum visdn_type_of_number type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN;

	if (!strcasecmp(str, "unknown"))
		type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN;
	else if (!strcasecmp(str, "international"))
		type_of_number = VISDN_TYPE_OF_NUMBER_INTERNATIONAL;
	else if (!strcasecmp(str, "national"))
		type_of_number = VISDN_TYPE_OF_NUMBER_NATIONAL;
	else if (!strcasecmp(str, "network_specific"))
		type_of_number = VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC;
	else if (!strcasecmp(str, "subscriber"))
		type_of_number = VISDN_TYPE_OF_NUMBER_SUBSCRIBER;
	else if (!strcasecmp(str, "abbreviated"))
		type_of_number = VISDN_TYPE_OF_NUMBER_ABBREVIATED;
	else {
		ast_log(LOG_ERROR,
			"Unknown type_of_number '%s'\n",
			str);
	}

	return type_of_number;
}

static enum q931_interface_network_role
	visdn_string_to_network_role(const char *str)
{
	enum q931_interface_network_role role = 0;

	if (!strcasecmp(str, "user"))
		role = Q931_INTF_NET_USER;
	else if (!strcasecmp(str, "private"))
		role = Q931_INTF_NET_PRIVATE;
	else if (!strcasecmp(str, "local"))
		role = Q931_INTF_NET_LOCAL;
	else if (!strcasecmp(str, "transit"))
		role = Q931_INTF_NET_TRANSIT;
	else if (!strcasecmp(str, "international"))
		role = Q931_INTF_NET_INTERNATIONAL;
	else {
		ast_log(LOG_ERROR,
			"Unknown network_role '%s'\n",
			str);
	}

	return role;
}

static int visdn_intf_from_var(
	struct visdn_interface *intf,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "network_role")) {
		intf->network_role = visdn_string_to_network_role(var->value);
	} else if (!strcasecmp(var->name, "type_of_number")) {
		intf->type_of_number = visdn_string_to_type_of_number(var->value);
	} else if (!strcasecmp(var->name, "local_type_of_number")) {
		intf->local_type_of_number = visdn_string_to_type_of_number(var->value);
	} else if (!strcasecmp(var->name, "tones_option")) {
		intf->tones_option = ast_true(var->value);
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(intf->context, var->value,
			sizeof(intf->context));
	} else if (!strcasecmp(var->name, "default_inbound_caller_id")) {
		strncpy(intf->default_inbound_caller_id, var->value,
			sizeof(intf->default_inbound_caller_id));
	} else if (!strcasecmp(var->name, "force_inbound_caller_id")) {
		intf->force_inbound_caller_id = ast_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_sending")) {
		intf->overlap_sending = ast_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_receiving")) {
		intf->overlap_receiving = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void visdn_copy_interface_config(
	struct visdn_interface *dst,
	const struct visdn_interface *src)
{
	dst->network_role = src->network_role;
	dst->type_of_number = src->type_of_number;
	dst->local_type_of_number = src->local_type_of_number;
	dst->tones_option = src->tones_option;
	strncpy(dst->context, src->context,
		sizeof(dst->context));
	strncpy(dst->default_inbound_caller_id, src->default_inbound_caller_id,
		sizeof(dst->default_inbound_caller_id));
	dst->force_inbound_caller_id = src->force_inbound_caller_id;
	dst->overlap_sending = src->overlap_sending;
	dst->overlap_receiving = src->overlap_receiving;
}

static void visdn_reload_config(void)
{
	struct ast_config *cfg;
	cfg = ast_load(VISDN_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s, VISDN disabled\n",
			VISDN_CONFIG_FILE);

		return;
	}

	struct ast_variable *var;
	var = ast_variable_browse(cfg, "general");
	while (var) {
		if (visdn_intf_from_var(&visdn.default_intf, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	int i;
	for (i=0; i<visdn.nifs; i++) {
		visdn.ifs[i].configured = FALSE;
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general"))
			continue;

		struct visdn_interface *intf = NULL;

		for (i=0; i<visdn.nifs; i++) {
			if (!strcasecmp(visdn.ifs[i].name, cat)) {
				intf = &visdn.ifs[i];
				break;
			}
		}

		if (!intf) {
			if (visdn.nifs >= sizeof(visdn.ifs)/sizeof(*visdn.ifs)) {
				ast_log(LOG_ERROR, "Too many interfaces!\n");
				return;
			}

			intf = &visdn.ifs[visdn.nifs];
			visdn.nifs++;

			intf->q931_intf = NULL;
			strncpy(intf->name, cat, sizeof(intf->name));
			visdn_copy_interface_config(intf, &visdn.default_intf);
		}

		intf->configured = TRUE;

		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (visdn_intf_from_var(intf, var) < 0) {
				ast_log(LOG_WARNING,
					"Unknown configuration variable %s\n",
					var->name);
			}

			var = var->next;
		}
	}

	ast_destroy(cfg);

}


static int do_visdn_reload(int fd, int argc, char *argv[])
{
	visdn_reload_config();

	return 0;
}

static int do_show_visdn_channels(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&q931_lock);

	int i;
	for (i=0; i<visdn.nifs; i++) {
		if (!visdn.ifs[i].q931_intf)
			continue;

		ast_cli(fd, "Interface: %s\n", visdn.ifs[i].name);

		int j;
		for (j=0; j<visdn.ifs[i].q931_intf->n_channels; j++) {
			ast_cli(fd, "  B%d: %s\n",
				visdn.ifs[i].q931_intf->channels[j].id + 1,
				q931_channel_state_to_text(
					visdn.ifs[i].q931_intf->channels[j].state));
		}
	}

	ast_mutex_unlock(&q931_lock);

	return 0;
}

/*
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member)	\
		     ;			\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member)	\
		     )
*/

static int visdn_cli_print_call_list(int fd, struct q931_interface *intf)
{
	int first_call;
	int i;
	for (i=0; i<visdn.nifs; i++) {
		if (!visdn.ifs[i].q931_intf)
			continue;

		struct q931_call *call;
		first_call = TRUE;

		list_for_each_entry(call, &visdn.ifs[i].q931_intf->calls, calls_node) {

			if (!intf || call->intf == intf) {

				if (first_call) {
					ast_cli(fd, "Interface: %s\n", visdn.ifs[i].q931_intf->name);
					ast_cli(fd, "  Ref#    Caller       Called       State\n");
					first_call = FALSE;
				}

				ast_cli(fd, "  %c %5ld %s\n",
					(call->direction == Q931_CALL_DIRECTION_INBOUND)
						? 'I' : 'O',
					call->call_reference,
					q931_call_state_to_text(call->state));

/*				ast_cli(fd, "  %c %5ld %-12s %-12s %s\n",
					(call->direction == Q931_CALL_DIRECTION_INBOUND)
						? 'I' : 'O',
					call->call_reference,
					call->calling_number,
					call->called_number,
					q931_call_state_to_text(call->state));
*/
			}
		}
	}

	return RESULT_SUCCESS;
}

static void visdn_cli_print_call(int fd, struct q931_call *call)
{
	ast_cli(fd, "--------- Call %ld %s (%d refs)\n",
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
			"inbound" : "outbound",
		call->refcnt);

	ast_cli(fd, "Interface       : %s\n", call->intf->name);

	if (call->dlc)
		ast_cli(fd, "DLC (TEI)       : %d\n", call->dlc->tei);

	ast_cli(fd, "State           : %s\n",
		q931_call_state_to_text(call->state));

//	ast_cli(fd, "Calling Number  : %s\n", call->calling_number);
//	ast_cli(fd, "Called Number   : %s\n", call->called_number);

//	ast_cli(fd, "Sending complete: %s\n",
//		call->sending_complete ? "Yes" : "No");

	ast_cli(fd, "Broadcast seutp : %s\n",
		call->broadcast_setup ? "Yes" : "No");

	ast_cli(fd, "Tones option    : %s\n",
		call->tones_option ? "Yes" : "No");

	ast_cli(fd, "Active timers   : ");

	if (call->T301.pending) ast_cli(fd, "T301 ");
	if (call->T302.pending) ast_cli(fd, "T302 ");
	if (call->T303.pending) ast_cli(fd, "T303 ");
	if (call->T304.pending) ast_cli(fd, "T304 ");
	if (call->T305.pending) ast_cli(fd, "T305 ");
	if (call->T306.pending) ast_cli(fd, "T306 ");
	if (call->T307.pending) ast_cli(fd, "T307 ");
	if (call->T308.pending) ast_cli(fd, "T308 ");
	if (call->T309.pending) ast_cli(fd, "T309 ");
	if (call->T310.pending) ast_cli(fd, "T310 ");
	if (call->T312.pending) ast_cli(fd, "T312 ");
	if (call->T313.pending) ast_cli(fd, "T313 ");
	if (call->T314.pending) ast_cli(fd, "T314 ");
	if (call->T316.pending) ast_cli(fd, "T316 ");
	if (call->T318.pending) ast_cli(fd, "T318 ");
	if (call->T319.pending) ast_cli(fd, "T319 ");
	if (call->T320.pending) ast_cli(fd, "T320 ");
	if (call->T321.pending) ast_cli(fd, "T321 ");
	if (call->T322.pending) ast_cli(fd, "T322 ");

	ast_cli(fd, "\n");

	ast_cli(fd, "CES:\n");
	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		ast_cli(fd, "%d %s %s ",
			ces->dlc->tei,
			q931_ces_state_to_text(ces->state),
			(ces == call->selected_ces ? "presel" :
			  (ces == call->preselected_ces ? "sel" : "")));

		if (ces->T304.pending) ast_cli(fd, "T304 ");
		if (ces->T308.pending) ast_cli(fd, "T308 ");
		if (ces->T322.pending) ast_cli(fd, "T322 ");

		ast_cli(fd, "\n");
	}

}

static int do_show_visdn_calls(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&q931_lock);

	if (argc == 3) {
		visdn_cli_print_call_list(fd, NULL);
	} else if (argc == 4) {
		struct q931_interface *intf = NULL;

		char *callpos = strchr(argv[3], '/');
		if (callpos) {
			*callpos = '\0';
			callpos++;
		}

		int i;
		for (i=0; i<visdn.nifs; i++) {
			if (visdn.ifs[i].q931_intf &&
			    !strcasecmp(visdn.ifs[i].name, argv[3])) {
				intf = visdn.ifs[i].q931_intf;
				break;
			}
		}

		if (!intf) {
			ast_cli(fd, "Interface not found\n");
			goto err_intf_not_found;
		}

		if (!callpos) {
			visdn_cli_print_call_list(fd, intf);
		} else {
			struct q931_call *call;

			if (callpos[0] == 'i' || callpos[0] == 'I') {
				call = q931_find_call_by_reference(intf,
					Q931_CALL_DIRECTION_INBOUND,
					atoi(callpos + 1));
			} else if (callpos[0] == 'o' || callpos[0] == 'O') {
				call = q931_find_call_by_reference(intf,
					Q931_CALL_DIRECTION_OUTBOUND,
					atoi(callpos + 1));
			} else {
				ast_cli(fd, "Invalid call reference\n");
				goto err_unknown_direction;
			}

			if (!call) {
				ast_cli(fd, "Call %s not found\n", callpos);
				goto err_call_not_found;
			}

			visdn_cli_print_call(fd, call);
		}
	}

err_call_not_found:
err_unknown_direction:
err_intf_not_found:

	ast_mutex_unlock(&q931_lock);

	return RESULT_SUCCESS;
}

static char debug_visdn_generic_help[] =
	"Usage: debug visdn generic\n"
	"	Debug generic vISDN events\n";

static struct ast_cli_entry debug_visdn_generic =
{
        { "debug", "visdn", "generic", NULL },
	do_debug_visdn_generic,
	"Enables generic vISDN debugging",
	debug_visdn_generic_help,
	NULL
};

static struct ast_cli_entry no_debug_visdn_generic =
{
        { "no", "debug", "visdn", "generic", NULL },
	do_no_debug_visdn_generic,
	"Disables generic vISDN debugging",
	NULL,
	NULL
};

static char debug_visdn_q921_help[] =
	"Usage: debug visdn q921 [interface]\n"
	"	Traces q921 traffic\n";

static struct ast_cli_entry debug_visdn_q921 =
{
        { "debug", "visdn", "q921", NULL },
	do_debug_visdn_q921,
	"Enables q.921 tracing",
	debug_visdn_q921_help,
	NULL
};

static struct ast_cli_entry no_debug_visdn_q921 =
{
        { "no", "debug", "visdn", "q921", NULL },
	do_no_debug_visdn_q921,
	"Disables q.921 tracing",
	NULL,
	NULL
};

static char debug_visdn_q931_help[] =
	"Usage: debug visdn q931 [interface]\n"
	"	Traces q931 traffic\n";

static struct ast_cli_entry debug_visdn_q931 =
{
        { "debug", "visdn", "q931", NULL },
	do_debug_visdn_q931,
	"Enables q.931 tracing",
	debug_visdn_q931_help,
	NULL
};

static struct ast_cli_entry no_debug_visdn_q931 =
{
        { "no", "debug", "visdn", "q931", NULL },
	do_no_debug_visdn_q931,
	"Disables q.931 tracing",
	NULL,
	NULL
};

static char show_visdn_interfaces_help[] =
	"Usage: visdn show interfaces\n"
	"	Displays informations on vISDN interfaces\n";

static struct ast_cli_entry show_visdn_interfaces =
{
        { "show", "visdn", "interfaces", NULL },
	do_show_visdn_interfaces,
	"Displays vISDN interface information",
	show_visdn_interfaces_help,
	NULL
};

static char visdn_visdn_reload_help[] =
	"Usage: visdn reload\n"
	"	Reloads vISDN config\n";

static struct ast_cli_entry visdn_reload =
{
        { "visdn", "reload", NULL },
	do_visdn_reload,
	"Reloads vISDN configuration",
	visdn_visdn_reload_help,
	NULL
};

static char visdn_show_visdn_channels_help[] =
	"Usage: visdn show channels\n"
	"	Displays informations on vISDN channels\n";

static struct ast_cli_entry show_visdn_channels =
{
        { "show", "visdn", "channels", NULL },
	do_show_visdn_channels,
	"Displays vISDN channel information",
	visdn_show_visdn_channels_help,
	NULL
};

static char show_visdn_calls_help[] =
	"Usage: show visdn calls\n"
	"	Lists vISDN calls\n";

static struct ast_cli_entry show_visdn_calls =
{
        { "show", "visdn", "calls", NULL },
	do_show_visdn_calls,
	"Lists vISDN calls",
	show_visdn_calls_help,
	NULL
};

static int visdn_devicestate(void *data)
{
	int res = AST_DEVICE_INVALID;

	// not sure what this should do xxx
	res = AST_DEVICE_UNKNOWN;
	return res;
}

static inline struct ast_channel *callpvt_to_astchan(
	struct q931_call *call)
{
	return (struct ast_channel *)call->pvt;
}

static enum q931_ie_called_party_number_type_of_number
	visdn_type_of_number_to_cdpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CDPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CDPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CDPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CDPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CDPN_TON_ABBREVIATED;
	}
}

static enum q931_ie_calling_party_number_type_of_number
	visdn_type_of_number_to_cgpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CGPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CGPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CGPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CGPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CGPN_TON_ABBREVIATED;
	}
}

static int visdn_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	int err = 0;
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

	ast_mutex_lock(&q931_lock);
	struct visdn_interface *intf = NULL;

	int i;
	for (i=0; i<visdn.nifs; i++) {
		if (visdn.ifs[i].q931_intf &&
		    !strcmp(visdn.ifs[i].name, intf_name)) {
			intf = &visdn.ifs[i];
			break;
		}
	}

	if (!intf) {
		ast_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		err = -1;
                goto err_intf_not_found;
	}

	if (!intf->q931_intf) {
		ast_log(LOG_WARNING, "Interface %s not present\n", intf_name);
		err = -1;
                goto err_intf_not_found;
	}

	struct q931_call *q931_call;
	q931_call = q931_alloc_call_out(intf->q931_intf);

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"visdn_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}
	
	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	q931_call->pvt = ast_chan;

	visdn_chan->q931_call = q931_call;

	char newname[40];
	snprintf(newname, sizeof(newname), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	ast_mutex_unlock(&ast_chan->lock);

	struct q931_ies ies = Q931_IES_INIT;

	struct q931_ie_bearer_capability *bc =
		q931_ie_bearer_capability_alloc();
	bc->coding_standard = Q931_IE_BC_CS_CCITT;
	bc->information_transfer_capability = Q931_IE_BC_ITC_SPEECH;
	bc->transfer_mode = Q931_IE_BC_TM_CIRCUIT;
	bc->information_transfer_rate = Q931_IE_BC_ITR_64;
	bc->user_information_layer_1_protocol = Q931_IE_BC_UIL1P_G711_ALAW;
	q931_ies_add_put(&ies, &bc->ie);

	struct q931_ie_called_party_number *cdpn =
		q931_ie_called_party_number_alloc();
	cdpn->type_of_number =
		visdn_type_of_number_to_cdpn(intf->type_of_number);
	cdpn->numbering_plan_identificator = Q931_IE_CDPN_NPI_ISDN_TELEPHONY;
	snprintf(cdpn->number, sizeof(cdpn->number), "%s", number);
	q931_ies_add_put(&ies, &cdpn->ie);

	if (intf->q931_intf->role == LAPD_ROLE_NT &&
	    !intf->overlap_receiving) {
		struct q931_ie_sending_complete *sc =
			q931_ie_sending_complete_alloc();

		q931_ies_add_put(&ies, &sc->ie);
	}

	if (ast_chan->callerid && strlen(ast_chan->callerid)) {

		char callerid[255];
		char *name, *number;

		strncpy(callerid, ast_chan->callerid, sizeof(callerid));
		ast_callerid_parse(callerid, &name, &number);

		if (number) {
			struct q931_ie_calling_party_number *cgpn =
				q931_ie_calling_party_number_alloc();

			cgpn->type_of_number =
				visdn_type_of_number_to_cgpn(intf->local_type_of_number);
			cgpn->numbering_plan_identificator =
				Q931_IE_CGPN_NPI_ISDN_TELEPHONY;
			cgpn->presentation_indicator =
				Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
			cgpn->screening_indicator =
				Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED;
			cgpn->number[0] = '\0';

			strncpy(cgpn->number, number, sizeof(cgpn->number));

			q931_ies_add_put(&ies, &cgpn->ie);
		} else {
			ast_log(LOG_WARNING,
				"Unable to parse '%s'"
				" into CallerID name & number\n",
				callerid);
		}
	}

	struct q931_ie_high_layer_compatibility *hlc =
		q931_ie_high_layer_compatibility_alloc();
	
	hlc->coding_standard = Q931_IE_HLC_CS_CCITT;
	hlc->interpretation = Q931_IE_HLC_P_FIRST;
	hlc->presentation_method = Q931_IE_HLC_PM_HIGH_LAYER_PROTOCOL_PROFILE;
	hlc->characteristics_identification = Q931_IE_HLC_CI_TELEPHONY;

	ast_mutex_lock(&q931_lock);
	q931_setup_request(q931_call, &ies);
	ast_mutex_unlock(&q931_lock);

err_channel_not_down:
err_intf_not_found:
	ast_mutex_unlock(&q931_lock);
err_invalid_format:
err_invalid_destination:

	return err;
}

static int visdn_answer(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	if (visdn.debug)
		ast_log(LOG_NOTICE, "visdn_answer\n");

//	ast_indicate(ast_chan, -1);

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return -1;
	}

	q931_setup_response(visdn_chan->q931_call, NULL);

	return 0;
}

static int visdn_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc)
{
	ast_log(LOG_WARNING, "visdn_bridge\n");

	return -2;

	/* if need DTMF, cant native bridge (at least not yet...) */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return -2;

	struct visdn_chan *visdn_chan1 = c0->pvt->pvt;
	struct visdn_chan *visdn_chan2 = c1->pvt->pvt;

	char path[100], dest1[100], dest2[100];

	snprintf(path, sizeof(path),
		"/sys/class/net/%s/device/../B%d",
		visdn_chan1->q931_call->intf->name,
		visdn_chan1->q931_call->channel->id+1);

	memset(dest1, 0x00, sizeof(dest1));
	if (readlink(path, dest1, sizeof(dest1) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		return -2;
	}

	char *chanid1 = strrchr(dest1, '/');
	if (!chanid1 || !strlen(chanid1 + 1)) {
		ast_log(LOG_ERROR, "Invalid chanid found in symlink %s\n", dest1);
		return -2;
	}

	chanid1++;

	snprintf(path, sizeof(path),
		"/sys/class/net/%s/device/../B%d",
		visdn_chan2->q931_call->intf->name,
		visdn_chan2->q931_call->channel->id+1);

	memset(dest2, 0x00, sizeof(dest2));
	if (readlink(path, dest2, sizeof(dest2) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		return -2;
	}

	char *chanid2 = strrchr(dest2, '/');
	if (!chanid2 || !strlen(chanid2 + 1)) {
		ast_log(LOG_ERROR, "Invalid chanid found in symlink %s\n", dest2);
		return -2;
	}

	chanid2++;

	if (visdn.debug)
		ast_log(LOG_NOTICE,
			"Connecting chan %s to chan %s\n",
			chanid1, chanid2);

	struct visdn_connect vc;
	snprintf(vc.src_chanid, sizeof(vc.src_chanid), "%s", chanid1);
	snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s", chanid2);
	vc.flags = 0;

	if (ioctl(visdn.control_fd, VISDN_IOC_CONNECT,
	    (caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR, "ioctl(VISDN_CONNECT): %s\n", strerror(errno));
		return -2;
	}

	struct ast_channel *cs[3];
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;

	for (;;) {
		struct ast_channel *who;
		int to;
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}

		struct ast_frame *f;
		f = ast_read(who);
		if (!f) {
			return 0;
		}

		printf("Frame %s\n", who->name);

		ast_frfree(f);
	}

	return 0;
}

struct ast_frame *visdn_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "visdn_exception\n");

	return NULL;
}

static int visdn_indicate(struct ast_channel *ast_chan, int condition)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return 1;
	}

	if (visdn.debug)
		ast_log(LOG_NOTICE, "visdn_indicate %d\n", condition);

	switch(condition) {
	case AST_CONTROL_HANGUP:
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
		return 1;
	break;

	case AST_CONTROL_RINGING: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_progress_indicator *pi = NULL;

		if (ast_chan->dialed &&
		   strcmp(ast_chan->dialed->type, VISDN_CHAN_TYPE)) {

			if (visdn.debug)
				ast_log(LOG_NOTICE,
					"Channel is not VISDN, sending"
					" progress indicator\n");

			pi = q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location = q931_ie_progress_indicator_location(
						visdn_chan->q931_call);
			pi->progress_description = Q931_IE_PI_PD_CALL_NOT_END_TO_END;
			q931_ies_add_put(&ies, &pi->ie);
		}

		ast_mutex_lock(&q931_lock);
		q931_alerting_request(visdn_chan->q931_call, &ies);
		ast_mutex_unlock(&q931_lock);

		return 1;
	}
	break;

	case AST_CONTROL_ANSWER:
//		q931_setup_response(visdn_chan->q931_call);
	break;

	case AST_CONTROL_BUSY: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(visdn_chan->q931_call);
		cause->value = Q931_IE_C_CV_USER_BUSY;
		q931_ies_add_put(&ies, &cause->ie);

		ast_mutex_lock(&q931_lock);
		q931_disconnect_request(visdn_chan->q931_call, &ies);
		ast_mutex_unlock(&q931_lock);

//		ast_softhangup_nolock(ast_chan, AST_SOFTHANGUP_DEV);
	}
	break;

	case AST_CONTROL_CONGESTION: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(visdn_chan->q931_call);
		cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
		q931_ies_add_put(&ies, &cause->ie);

		ast_mutex_lock(&q931_lock);
		q931_disconnect_request(visdn_chan->q931_call, &ies);
		ast_mutex_unlock(&q931_lock);

//		ast_softhangup_nolock(ast_chan, AST_SOFTHANGUP_DEV);
	}
	break;

	case AST_CONTROL_PROGRESS: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_progress_indicator *pi =
			q931_ie_progress_indicator_alloc();
		pi->coding_standard = Q931_IE_PI_CS_CCITT;
		pi->location = q931_ie_progress_indicator_location(
					visdn_chan->q931_call);

		if (ast_chan->dialed &&
		   strcmp(ast_chan->dialed->type, VISDN_CHAN_TYPE)) {
			pi->progress_description =
				Q931_IE_PI_PD_CALL_NOT_END_TO_END; // FIXME
		} else {
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
		}

		q931_ies_add_put(&ies, &pi->ie);

		ast_mutex_lock(&q931_lock);
		q931_progress_request(visdn_chan->q931_call, &ies);
		ast_mutex_unlock(&q931_lock);
	}
	break;

	case AST_CONTROL_PROCEEDING:
		ast_mutex_lock(&q931_lock);
		q931_proceeding_request(visdn_chan->q931_call, NULL);
		ast_mutex_unlock(&q931_lock);
	break;
	}

	return 1;
}

static int visdn_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_setoption(struct ast_channel *ast_chan, int option, void *data, int datalen)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_transfer(struct ast_channel *ast, char *dest)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_send_digit(struct ast_channel *ast_chan, char digit)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	struct q931_call *q931_call = visdn_chan->q931_call;
	struct visdn_interface *intf = q931_call->intf->pvt;

	struct q931_ies ies = Q931_IES_INIT;

	struct q931_ie_called_party_number *cdpn =
		q931_ie_called_party_number_alloc();
	cdpn->type_of_number = visdn_type_of_number_to_cdpn(intf->type_of_number);
	cdpn->numbering_plan_identificator = Q931_IE_CDPN_NPI_ISDN_TELEPHONY;
	cdpn->number[0] = digit;
	cdpn->number[1] = '\0';
	q931_ies_add_put(&ies, &cdpn->ie);

	ast_mutex_lock(&q931_lock);
	q931_info_request(q931_call, &ies);
	ast_mutex_unlock(&q931_lock);

	return 1;
}

static int visdn_sendtext(struct ast_channel *ast, char *text)
{
	ast_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void visdn_destroy(struct visdn_chan *visdn_chan)
{
//	if (visdn_chan->ec)
//		echo_can_free(visdn_chan->ec);

	free(visdn_chan);
}

static struct visdn_chan *visdn_alloc()
{
	struct visdn_chan *visdn_chan;

	visdn_chan = malloc(sizeof(struct visdn_chan));
	if (!visdn_chan)
		return NULL;

	memset(visdn_chan, 0x00, sizeof(struct visdn_chan));

	visdn_chan->channel_fd = -1;

//	visdn_chan->ec = echo_can_create(256, 0);

	return visdn_chan;
}

static int visdn_generator_stop(struct ast_channel *chan);

static int visdn_hangup(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	if (visdn.debug)
		ast_log(LOG_NOTICE, "visdn_hangup\n");

	if (!visdn_chan) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	// Make sure the generator is stopped
	if (!ast_chan->pbx)
		visdn_generator_stop(ast_chan);

	struct q931_call *q931_call = visdn_chan->q931_call;

	visdn_destroy(visdn_chan);

	ast_chan->pvt->pvt = NULL;

	if (q931_call) {
		q931_call->pvt = NULL;

		if (q931_call->state != N0_NULL_STATE &&
		    q931_call->state != N1_CALL_INITIATED &&
		    q931_call->state != N11_DISCONNECT_REQUEST &&
		    q931_call->state != N12_DISCONNECT_INDICATION &&
		    q931_call->state != N17_RESUME_REQUEST &&
		    q931_call->state != N19_RELEASE_REQUEST &&
		    q931_call->state != N22_CALL_ABORT &&
		    q931_call->state != U0_NULL_STATE &&
		    q931_call->state != U6_CALL_PRESENT &&
		    q931_call->state != U11_DISCONNECT_REQUEST &&
		    q931_call->state != U12_DISCONNECT_INDICATION &&
		    q931_call->state != U15_SUSPEND_REQUEST &&
		    q931_call->state != U17_RESUME_REQUEST &&
		    q931_call->state != U19_RELEASE_REQUEST) {

                        struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NORMAL_CALL_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			ast_mutex_lock(&q931_lock);
			q931_disconnect_request(q931_call, &ies);
			ast_mutex_unlock(&q931_lock);
		}
	}

	ast_setstate(ast_chan, AST_STATE_DOWN);

	return 0;
}

static struct ast_frame *visdn_read(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	static struct ast_frame f;
	static char buf[512];

	if (visdn_chan->channel_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;
		f.src = VISDN_CHAN_TYPE;
		f.mallocd = 0;
		f.delivery.tv_sec = 0;
		f.delivery.tv_usec = 0;

		return &f;
	}

	int nread = read(visdn_chan->channel_fd, buf, 512);

/*	for (i=0; i<nread; i++) {
		buf[i] = linear_to_alaw(
				echo_can_update(visdn_chan->ec,
					alaw_to_linear(txbuf[i]),
					alaw_to_linear(buf[i])));
	}*/

/*struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "R %.3f %d %d\n", t/1000000.0, visdn_chan->channel_fd, r);*/

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = nread;
	f.datalen = nread;
	f.data = buf;
	f.offset = 0;
	f.src = VISDN_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	return &f;
}

static int visdn_write(struct ast_channel *ast_chan, struct ast_frame *frame)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

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
		return -1;
	}

	if (visdn_chan->channel_fd < 0) {
//		ast_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

/*ast_verbose(VERBOSE_PREFIX_3 "W %d %02x%02x%02x%02x%02x%02x%02x%02x %d\n", visdn_chan->channel_fd,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);*/

	write(visdn_chan->channel_fd, frame->data, frame->datalen);

	return 0;
}

static struct ast_channel *visdn_new(
	struct visdn_chan *visdn_chan,
	int state)
{
	struct ast_channel *ast_chan;
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		return NULL;
	}

        ast_chan->type = VISDN_CHAN_TYPE;

	ast_chan->fds[0] = visdn.timer_fd;

	if (state == AST_STATE_RING)
		ast_chan->rings = 1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->pvt->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->pvt->rawwriteformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;

//	ast_chan->language[0] = '\0';
//	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	visdn_chan->ast_chan = ast_chan;
	ast_chan->pvt->pvt = visdn_chan;

	ast_chan->pvt->call = visdn_call;
	ast_chan->pvt->hangup = visdn_hangup;
	ast_chan->pvt->answer = visdn_answer;
	ast_chan->pvt->read = visdn_read;
	ast_chan->pvt->write = visdn_write;
	ast_chan->pvt->bridge = visdn_bridge;
	ast_chan->pvt->exception = visdn_exception;
	ast_chan->pvt->indicate = visdn_indicate;
	ast_chan->pvt->fixup = visdn_fixup;
	ast_chan->pvt->setoption = visdn_setoption;
	ast_chan->pvt->send_text = visdn_sendtext;
	ast_chan->pvt->transfer = visdn_transfer;
	ast_chan->pvt->send_digit = visdn_send_digit;

	ast_setstate(ast_chan, state);

	return ast_chan;
}

static struct ast_channel *visdn_request(char *type, int format, void *data)
{
	struct visdn_chan *visdn_chan;
	char *dest = NULL;

	if (!(format & AST_FORMAT_ALAW)) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		return NULL;
	}

	if (data) {
		dest = ast_strdupa((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}

	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		return NULL;
	}

	struct ast_channel *ast_chan;
	ast_chan = visdn_new(visdn_chan, AST_STATE_DOWN);

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/null");

	ast_mutex_lock(&usecnt_lock);
	visdn.usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	return ast_chan;
}

void refresh_polls_list(
	struct pollfd *polls,
	struct poll_info *poll_infos,
	int *npolls)
{
	*npolls = 0;

	polls[*npolls].fd = visdn.netlink_socket;
	polls[*npolls].events = POLLIN | POLLERR;
	poll_infos[*npolls].type = POLL_INFO_TYPE_NETLINK;
	poll_infos[*npolls].interface = NULL;
	(*npolls)++;

	int i;
	for(i = 0; i < visdn.nifs; i++) {
		if (!visdn.ifs[i].q931_intf)
			continue;

		if (visdn.ifs[i].q931_intf->role == LAPD_ROLE_NT) {
			polls[*npolls].fd = visdn.ifs[i].q931_intf->master_socket;
			polls[*npolls].events = POLLIN | POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_INTERFACE;
			poll_infos[*npolls].interface = visdn.ifs[i].q931_intf;
			(*npolls)++;
		} else {
			polls[*npolls].fd = visdn.ifs[i].q931_intf->dlc.socket;
			polls[*npolls].events = POLLIN | POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = &visdn.ifs[i].q931_intf->dlc;
			(*npolls)++;
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc,  &visdn.ifs[i].q931_intf->dlcs, intf_node) {
			polls[*npolls].fd = dlc->socket;
			polls[*npolls].events = POLLIN | POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = dlc;
			(*npolls)++;
		}
	}
}

static void visdn_accept(
	struct q931_interface *intf,
	int accept_socket)
{
	struct q931_dlc *newdlc;

	newdlc = q931_accept(intf, accept_socket);
	if (!newdlc)
		return;

	if (visdn.debug)
		ast_log(LOG_NOTICE,
			"New DLC (TEI=%d) accepted on interface %s\n",
			newdlc->tei,
			intf->name);

	refresh_polls_list(
		visdn.polls,
		visdn.poll_infos,
		&visdn.npolls);
}

void visdn_add_interface(const char *name)
{
	struct visdn_interface *intf = NULL;

	int i;
	for (i=0; i<visdn.nifs; i++) {
		if (!strcasecmp(visdn.ifs[i].name, name)) {
			intf = &visdn.ifs[i];
			break;
		}
	}

	if (!intf) {
		intf = &visdn.ifs[visdn.nifs];
		visdn.nifs++;

		intf->q931_intf = NULL;
		strncpy(intf->name, name, sizeof(intf->name));
		visdn_copy_interface_config(intf, &visdn.default_intf);
	}

	if (intf->q931_intf)
		return;

	if (visdn.debug)
		ast_log(LOG_NOTICE, "Opening interface %s\n", name);

	intf->q931_intf = q931_open_interface(visdn.libq931, name, 0);
	if (!intf->q931_intf) {
		ast_log(LOG_WARNING,
			"Cannot open interface %s, skipping\n",
			name);

		return;
	}

	intf->q931_intf->pvt = intf;
	intf->q931_intf->network_role = intf->network_role;

	if (intf->q931_intf->role == LAPD_ROLE_NT) {
		if (listen(intf->q931_intf->master_socket, 100) < 0) {
			ast_log(LOG_ERROR,
				"cannot listen on master socket: %s\n",
				strerror(errno));

				return;
		}
	}

}

void visdn_rem_interface(const char *name)
{
	int i;
	for (i=0; i<visdn.nifs; i++) {
		if (visdn.ifs[i].q931_intf &&
		    !strcmp(visdn.ifs[i].name, name)) {
			q931_close_interface(visdn.ifs[i].q931_intf);

			visdn.ifs[i].q931_intf = NULL;

			break;
		}
	}
}

static void visdn_netlink_receive()
{
	struct sockaddr_nl tonl;
	tonl.nl_family = AF_NETLINK;
	tonl.nl_pid = 0;
	tonl.nl_groups = 0;

	struct msghdr skmsg;
	struct sockaddr_nl nl;
	struct cmsghdr cmsg;
	struct iovec iov;

	__u8 data[1024];

	struct nlmsghdr *hdr = (struct nlmsghdr *)data;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	skmsg.msg_name = &nl;
	skmsg.msg_namelen = sizeof(nl);
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	if(recvmsg(visdn.netlink_socket, &skmsg, 0) < 0) {
		ast_log(LOG_WARNING, "recvmsg: %s\n", strerror(errno));
		return;
	}

	if (hdr->nlmsg_type == RTM_NEWLINK) {
		struct ifinfomsg *ifi =
			(struct ifinfomsg *)(data + sizeof(*hdr));

		if (ifi->ifi_type == ARPHRD_LAPD) {

			int off = sizeof(*hdr) + sizeof(*ifi);
			char ifname[IFNAMSIZ] = "";

			while(off < hdr->nlmsg_len) {
				struct rtattr *rtattr =
					(struct rtattr *)(data + off);

				if (rtattr->rta_type == IFLA_IFNAME)
					strncpy(ifname,
						data + off + sizeof(*rtattr),
						sizeof(ifname));

				off += rtattr->rta_len;
			}

			ast_mutex_lock(&q931_lock);

			if (ifi->ifi_flags & IFF_UP) {
				if (visdn.debug)
					ast_log(LOG_NOTICE,
						 "Netlink msg: %s UP %s\n",
						ifname,
						(ifi->ifi_flags & IFF_ALLMULTI) ? "NT": "TE");

				visdn_add_interface(ifname);
			} else {
				if (visdn.debug)
					ast_log(LOG_NOTICE,
						 "Netlink msg: %s DOWN %s\n",
						ifname,
						(ifi->ifi_flags & IFF_ALLMULTI) ? "NT": "TE");

				visdn_rem_interface(ifname);
			}

			ast_mutex_unlock(&q931_lock);
		}
	}
}

static int visdn_q931_thread_do_poll()
{
	longtime_t usec_to_wait = q931_run_timers(visdn.libq931);
	int msec_to_wait;

	if (usec_to_wait < 0)
		msec_to_wait = -1;
	else
		msec_to_wait = usec_to_wait / 1000 + 1;

	if (visdn.debug)
		ast_verbose(VERBOSE_PREFIX_3
			"Time to wait = %d\n",
			msec_to_wait);

	if (poll(visdn.polls, visdn.npolls, msec_to_wait) < 0) {
		if (errno == EINTR)
			return TRUE;

		ast_log(LOG_WARNING, "poll error: %s\n", strerror(errno));
		exit(1);
	}

	int i;
	for(i = 0; i < visdn.npolls; i++) {
		if (visdn.poll_infos[i].type == POLL_INFO_TYPE_NETLINK) {
			if (visdn.polls[i].revents & POLLIN) {
				visdn_netlink_receive();
			}
		} else if (visdn.poll_infos[i].type == POLL_INFO_TYPE_INTERFACE) {
			if (visdn.polls[i].revents & POLLERR) {
				ast_log(LOG_WARNING, "Error on interface %s poll\n",
					visdn.poll_infos[i].interface->name);

				visdn.polls[i].fd = -1;
			}

			if (visdn.polls[i].revents & POLLIN) {
				ast_mutex_lock(&q931_lock);
				visdn_accept(
					visdn.poll_infos[i].interface,
					visdn.polls[i].fd);
				ast_mutex_unlock(&q931_lock);
			}
		} else if (visdn.poll_infos[i].type == POLL_INFO_TYPE_DLC) {
			if (visdn.polls[i].revents & POLLERR ||
			    visdn.polls[i].revents & POLLIN) {
				ast_mutex_lock(&q931_lock);
				q931_receive(visdn.poll_infos[i].dlc);
				ast_mutex_unlock(&q931_lock);
			}
		}
	}

	int active_calls_cnt = 0;
	if (visdn.have_to_exit) {
		active_calls_cnt = 0;

		for (i=0; i<visdn.nifs; i++) {
			if (visdn.ifs[i].q931_intf) {
				struct q931_call *call;
				list_for_each_entry(call,
						&visdn.ifs[i].q931_intf->calls,
						calls_node)
					active_calls_cnt++;
			}
		}

		ast_log(LOG_WARNING,
			"There are still %d active calls, waiting...\n",
			active_calls_cnt);
	}

	return (!visdn.have_to_exit || active_calls_cnt > 0);
}


static void *visdn_q931_thread_main(void *data)
{

	visdn.npolls = 0;
	refresh_polls_list(
		visdn.polls,
		visdn.poll_infos,
		&visdn.npolls);

	visdn.have_to_exit = 0;

	while(visdn_q931_thread_do_poll());

	return NULL;
}

#define FUNC_DEBUG()					\
	if (visdn.debug)				\
		ast_verbose(VERBOSE_PREFIX_3		\
			"%s\n", __FUNCTION__);		\

static void visdn_q931_alerting_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_queue_control(ast_chan, AST_CONTROL_RINGING);
	ast_setstate(ast_chan, AST_STATE_RINGING);
}

static void visdn_q931_connect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	ast_mutex_lock(&q931_lock);
	q931_setup_complete_request(q931_call, NULL);
	ast_mutex_unlock(&q931_lock);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
}

static void visdn_q931_disconnect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
	}

	ast_mutex_lock(&q931_lock);
	q931_release_request(q931_call, NULL);
	ast_mutex_unlock(&q931_lock);
}

static void visdn_q931_error_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static int visdn_handle_called_number(
	struct visdn_chan *visdn_chan,
	const struct q931_ie_called_party_number *ie)
{
	if (strlen(visdn_chan->called_number) + strlen(ie->number) - 1 >
			sizeof(visdn_chan->called_number)) {
		ast_log(LOG_NOTICE,
			"Called number overflow\n");

		return FALSE;
	}

	if (ie->number[strlen(ie->number) - 1] == '#') {
		visdn_chan->sending_complete = TRUE;
		strncat(visdn_chan->called_number, ie->number,
			strlen(ie->number)-1);
	} else {
		strcat(visdn_chan->called_number, ie->number);
	}

	return TRUE;
}



static void visdn_q931_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	struct visdn_interface *intf = q931_call->intf->pvt;

	if (!ast_chan)
		return;

	if (q931_call->state != U2_OVERLAP_SENDING &&
	    q931_call->state != N2_OVERLAP_SENDING) {
		ast_log(LOG_WARNING, "Received info not in overlap sending\n");
		return;
	}

	struct q931_ie_called_party_number *cdpn = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->type->id == Q931_IE_CALLED_PARTY_NUMBER) {
			cdpn = container_of(ies->ies[i],
				struct q931_ie_called_party_number, ie);
		}
	}

	if (ast_chan->pbx) {
		if (!cdpn)
			return;

ast_log(LOG_WARNING, "Trying to send DTMF FRAME\n");

		for(i=0; cdpn->number[i]; i++) {
			struct ast_frame f = { AST_FRAME_DTMF, cdpn->number[i] };
			ast_queue_frame(ast_chan, &f);
		}

		return;
	}

	if (!visdn_handle_called_number(visdn_chan, cdpn)) {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(q931_call);
		cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
		q931_ies_add_put(&ies, &cause->ie);

		ast_mutex_lock(&q931_lock);
		q931_disconnect_request(q931_call, &ies);
		ast_mutex_unlock(&q931_lock);

		return;
	}

	ast_setstate(ast_chan, AST_STATE_DIALING);

	if (visdn_chan->sending_complete) {
		if (ast_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(ast_chan->exten,
				visdn_chan->called_number,
				sizeof(ast_chan->exten)-1);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_disconnect_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);
			} else {
				ast_mutex_lock(&q931_lock);
				q931_proceeding_request(q931_call, NULL);
				ast_mutex_unlock(&q931_lock);

				ast_setstate(ast_chan, AST_STATE_RING);
			}
		} else {
	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_UNALLOCATED_NUMBER;
			q931_ies_add_put(&ies, &cause->ie);

			ast_mutex_lock(&q931_lock);
			q931_disconnect_request(q931_call, &ies);
			ast_mutex_unlock(&q931_lock);
		}
	} else {
		if (!ast_canmatch_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			ast_mutex_lock(&q931_lock);
			q931_disconnect_request(q931_call, &ies);
			ast_mutex_unlock(&q931_lock);

			return;
		}

		if (ast_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(ast_chan->exten,
				visdn_chan->called_number,
				sizeof(ast_chan->exten)-1);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_disconnect_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);
			}

			if (!ast_matchmore_extension(NULL, intf->context,
					visdn_chan->called_number, 1,
					visdn_chan->calling_number)) {

				ast_mutex_lock(&q931_lock);
				q931_proceeding_request(q931_call, NULL);
				ast_mutex_unlock(&q931_lock);
			}
		}
	}
}

static void visdn_q931_more_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_notify_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_proceeding_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);
}

static void visdn_q931_progress_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_queue_control(ast_chan, AST_CONTROL_PROGRESS);
}

static void visdn_q931_reject_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
}

static void visdn_q931_release_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_release_confirm_status status)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	if (ast_chan->pbx)
		ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
	else
		ast_hangup(ast_chan);
}

static void visdn_q931_release_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
}

static void visdn_q931_resume_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_resume_confirm_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_resume_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	__u8 *call_identity,
	int call_identity_len)
{
	FUNC_DEBUG();

	q931_resume_response(q931_call, NULL);
}

static void visdn_q931_setup_complete_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_complete_indication_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_setup_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_confirm_status status)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_setstate(ast_chan, AST_STATE_UP);
}

static void visdn_q931_setup_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		goto err_visdn_alloc;
	}

	visdn_chan->q931_call = q931_call;

	struct visdn_interface *intf = q931_call->intf->pvt;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->type->id == Q931_IE_CALLED_PARTY_NUMBER) {
			struct q931_ie_called_party_number *cdpn =
				container_of(ies->ies[i],
					struct q931_ie_called_party_number, ie);

			if (!visdn_handle_called_number(visdn_chan, cdpn)) {

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_reject_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);

				return;
			}
		} else if (ies->ies[i]->type->id == Q931_IE_CALLING_PARTY_NUMBER) {
			struct q931_ie_calling_party_number *cgpn =
				container_of(ies->ies[i],
					struct q931_ie_calling_party_number, ie);

			strncpy(visdn_chan->calling_number, cgpn->number,
				sizeof(visdn_chan->calling_number));

		} else if (ies->ies[i]->type->id == Q931_IE_BEARER_CAPABILITY) {
			struct q931_ie_bearer_capability *bc =
				container_of(ies->ies[i],
					struct q931_ie_bearer_capability, ie);

			if (bc->information_transfer_capability ==
					Q931_IE_BC_ITC_SPEECH ||
			    bc->information_transfer_capability ==
					Q931_IE_BC_ITC_3_1_KHZ_AUDIO) {

				q931_call->tones_option = intf->tones_option;
			} else {
		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location =
					q931_ie_cause_location_call(q931_call);
				cause->value =
					Q931_IE_C_CV_BEARER_CAPABILITY_NOT_IMPLEMENTED;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_reject_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);

				return;
			}
		}
	}

	struct ast_channel *ast_chan;
	ast_chan = visdn_new(visdn_chan, AST_STATE_OFFHOOK);
	if (!ast_chan)
		goto err_visdn_new;

	q931_call->pvt = ast_chan;

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);

	strncpy(ast_chan->context,
		intf->context,
		sizeof(ast_chan->context)-1);

	ast_mutex_lock(&usecnt_lock);
	visdn.usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	if (strlen(visdn_chan->calling_number) && !intf->force_inbound_caller_id)
		ast_set_callerid(ast_chan, visdn_chan->calling_number, 0);
	else
		ast_set_callerid(ast_chan, intf->default_inbound_caller_id, 0);

	if (!intf->overlap_sending ||
	    visdn_chan->sending_complete) {
		if (ast_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(ast_chan->exten,
				visdn_chan->called_number,
				sizeof(ast_chan->exten)-1);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_reject_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);
			} else {
				ast_mutex_lock(&q931_lock);
				q931_proceeding_request(q931_call, NULL);
				ast_mutex_unlock(&q931_lock);

				ast_setstate(ast_chan, AST_STATE_RING);
			}
		} else {
			ast_log(LOG_NOTICE,
				"No extension '%s' in context '%s',"
				" rejecting call\n",
				visdn_chan->called_number,
				intf->context);

	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			ast_mutex_lock(&q931_lock);
			q931_reject_request(q931_call, &ies);
			ast_mutex_unlock(&q931_lock);
		}

	} else {
		if (!ast_canmatch_extension(NULL,
				intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			ast_mutex_lock(&q931_lock);
			q931_reject_request(q931_call, &ies);
			ast_mutex_unlock(&q931_lock);

			ast_hangup(ast_chan);

			return;
		}

		if (ast_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(ast_chan->exten,
				visdn_chan->called_number,
				sizeof(ast_chan->exten)-1);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				ast_mutex_lock(&q931_lock);
				q931_reject_request(q931_call, &ies);
				ast_mutex_unlock(&q931_lock);
			} else {
				ast_setstate(ast_chan, AST_STATE_RING);

				if (!ast_matchmore_extension(NULL, intf->context,
						visdn_chan->called_number, 1,
						visdn_chan->calling_number)) {

					ast_mutex_lock(&q931_lock);
					q931_proceeding_request(q931_call, NULL);
					ast_mutex_unlock(&q931_lock);
				} else {
					ast_mutex_lock(&q931_lock);
					q931_more_info_request(q931_call, NULL);
					ast_mutex_unlock(&q931_lock);
				}
			}
		} else {
			ast_mutex_lock(&q931_lock);
			q931_more_info_request(q931_call, NULL);
			ast_mutex_unlock(&q931_lock);
		}
	}

	return;

err_visdn_new:
	// Free visdn_chan
err_visdn_alloc:
;
}

static void visdn_q931_status_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_status_indication_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_suspend_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_suspend_confirm_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_suspend_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	__u8 *call_identity,
	int call_identity_len)
{
	FUNC_DEBUG();

	q931_suspend_response(q931_call, NULL);
}

static void visdn_q931_timeout_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_connect_channel(struct q931_channel *channel)
{
	FUNC_DEBUG();

	assert(channel);
	assert(channel->call);

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	assert(ast_chan);
	assert(ast_chan->pvt);
	assert(ast_chan->pvt->pvt);

	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

/*
enum sb_bearertype
{
        VISDN_BT_VOICE  = 1,
        VISDN_BT_PPP    = 2,
};

struct sb_setbearer
{
        int sb_index;
        enum sb_bearertype sb_bearertype;
};

#define VISDN_SET_BEARER        SIOCDEVPRIVATE
#define VISDN_PPP_GET_CHAN      (SIOCDEVPRIVATE+1)
#define VISDN_PPP_GET_UNIT      (SIOCDEVPRIVATE+2)

	struct ifreq ifr;
	strncpy(ifr.ifr_name, channel->call->intf->name, sizeof(ifr.ifr_name));

	struct sb_setbearer bt;
	bt.sb_index = channel->id;
	bt.sb_bearertype = VISDN_BT_VOICE;
	ifr.ifr_data = (void *)&bt;

	if (ioctl(channel->call->dlc->socket, VISDN_SET_BEARER,
	    (caddr_t) &ifr) < 0) {
		ast_log(LOG_ERROR, "ioctl(VISDN_SET_BEARER): %s\n", strerror(errno));
		return;
	}
*/

	char path[100], dest[100];
	snprintf(path, sizeof(path),
		"/sys/class/net/%s/device/../B%d",
		channel->call->intf->name,
		channel->id+1);

	memset(dest, 0x00, sizeof(dest));
	if (readlink(path, dest, sizeof(dest) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		return;
	}

	char *chanid = strrchr(dest, '/');
	if (!chanid || !strlen(chanid + 1)) {
		ast_log(LOG_ERROR, "Invalid chanid found in symlink %s\n", dest);
		return;
	}

	chanid++;

	if (visdn.debug)
		ast_log(LOG_NOTICE,
			"Connecting softport to chan %s\n",
			chanid);

	visdn_chan->channel_fd = open("/dev/visdn/softport", O_RDWR);
	if (visdn_chan->channel_fd < 0) {
		ast_log(LOG_ERROR, "Cannot open softport: %s\n", strerror(errno));
		return;
	}

	struct visdn_connect vc;
	strcpy(vc.src_chanid, "");
	snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s", chanid);
	vc.flags = 0;

	if (ioctl(visdn_chan->channel_fd, VISDN_IOC_CONNECT,
	    (caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR, "ioctl(VISDN_CONNECT): %s\n", strerror(errno));
		return;
	}
}

static void visdn_q931_disconnect_channel(struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan) 
		return;

	// FIXME
	if (ast_chan->generator)
		visdn_generator_stop(ast_chan);

	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	close(visdn_chan->channel_fd);
}

static pthread_t visdn_generator_thread = AST_PTHREADT_NULL;
AST_MUTEX_DEFINE_STATIC(gen_chans_lock);
static struct ast_channel *gen_chans[256];
static int gen_chans_num = 0;

static void *visdn_generator_thread_main(void *aaa)
{
	struct ast_frame f;
	__u8 buf[256];

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = 80;
	f.datalen = 80;
	f.data = buf;
	f.offset = 0;
	f.src = VISDN_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	int ms = 500;

	if (visdn.debug)
		ast_log(LOG_NOTICE, "Generator thread started\n");

	while (gen_chans_num) {
		struct ast_channel *chan;

		chan = ast_waitfor_n(gen_chans, gen_chans_num, &ms);
		if (chan) {
			void *tmp;
			int res;
                	int (*generate)(struct ast_channel *chan, void *tmp,
			int datalen, int samples);

			tmp = chan->generatordata;
			chan->generatordata = NULL;
			generate = chan->generator->generate;
			res = generate(chan, tmp, f.datalen, f.samples);
			chan->generatordata = tmp;
			if (res) {
			        ast_log(LOG_DEBUG, "Auto-deactivating generator\n");
			        ast_deactivate_generator(chan);
			}
		}
	}

	visdn_generator_thread = AST_PTHREADT_NULL;

	if (visdn.debug)
		ast_log(LOG_NOTICE, "Generator thread stopped\n");

	return NULL;
}

static int visdn_generator_start(struct ast_channel *chan)
{
	int res = -1;

	ast_mutex_lock(&gen_chans_lock);

	int i;
	for (i=0; i<gen_chans_num; i++) {
		if (gen_chans[i] == chan)
			goto already_generating;
	}

	if (gen_chans_num > sizeof(gen_chans)) {
		ast_log(LOG_WARNING, "MAX 256 chans in dialtone generation\n");
		goto err_too_many_channels;
	}

	gen_chans[gen_chans_num] = chan;
	gen_chans_num++;

	if (visdn_generator_thread == AST_PTHREADT_NULL) {
		if (ast_pthread_create(&visdn_generator_thread, NULL,
				visdn_generator_thread_main, NULL)) {
			ast_log(LOG_WARNING, "Unable to create autoservice thread :(\n");
		} else
			pthread_kill(visdn_generator_thread, SIGURG);
	}

err_too_many_channels:
already_generating:
	ast_mutex_unlock(&gen_chans_lock);

	return res;
}

static int visdn_generator_stop(struct ast_channel *chan)
{
	ast_mutex_lock(&gen_chans_lock);

	int i;
	for (i=0; i<gen_chans_num; i++) {
		if (gen_chans[i] == chan) {
			int j;
			for (j=i; j<gen_chans_num-1; j++)
				gen_chans[j] = gen_chans[j+1];

			break;
		}
	}

	if (i == gen_chans_num)
		goto err_chan_not_found;

	gen_chans_num--;


	if (gen_chans_num == 0 &&
	    visdn_generator_thread != AST_PTHREADT_NULL) {
		pthread_kill(visdn_generator_thread, SIGURG);
	}

	ast_mutex_unlock(&gen_chans_lock);

	/* Wait for it to un-block */
	while(chan->blocking)
		usleep(1000);

	return 0;

err_chan_not_found:

	ast_mutex_unlock(&gen_chans_lock);

	return 0;
}




static void visdn_q931_start_tone(struct q931_channel *channel,
	enum q931_tone_type tone)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	// Unfortunately, after ast_hangup the channel is not valid
	// anymore and we cannot generate further tones thought we should
	if (!ast_chan)
		return;

	switch (tone) {
	case Q931_TONE_DIAL: {
		struct tone_zone_sound *ts;
		ts = ast_get_indication_tone(ast_chan->zone, "dial");

		ast_mutex_lock(&ast_chan->lock);
		if (ts) {
			if (ast_playtones_start(ast_chan, 0, ts->data, 0))
				ast_log(LOG_WARNING,
					"Unable to start playtones\n");
		} else {
			ast_tonepair_start(ast_chan, 350, 440, 0, 0);
		}
		ast_mutex_unlock(&ast_chan->lock);

		visdn_generator_start(ast_chan);
	}
	break;

	case Q931_TONE_HANGUP: {
		struct tone_zone_sound *ts;
		ts = ast_get_indication_tone(ast_chan->zone, "congestion");

		ast_mutex_lock(&ast_chan->lock);
		if (ts) {
			if (ast_playtones_start(ast_chan, 0, ts->data, 0))
				ast_log(LOG_WARNING,
					"Unable to start playtones\n");
		} else {
			ast_tonepair_start(ast_chan, 350, 440, 0, 0);
		}
		ast_mutex_unlock(&ast_chan->lock);

		if (!ast_chan->pbx)
			visdn_generator_start(ast_chan);
	}
	break;

/*
	case Q931_TONE_BUSY:
	break;

	case Q931_TONE_FAILURE:
	break;*/
	default:;
	}
}



static void visdn_q931_stop_tone(struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	visdn_generator_stop(ast_chan);
}

static void visdn_q931_management_restart_confirm(
	struct q931_global_call *gc,
	const struct q931_chanset *chanset)
{
	FUNC_DEBUG();
}

static void visdn_q931_timeout_management_indication(
	struct q931_global_call *gc)
{
	FUNC_DEBUG();
}

static void visdn_q931_status_management_indication(
	struct q931_global_call *gc)
{
	FUNC_DEBUG();
}

static void visdn_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case Q931_LOG_DEBUG:
		if (visdn.debug_q931)
			ast_verbose(VERBOSE_PREFIX_3 "%s", msg);
	break;

	case Q931_LOG_INFO: ast_verbose(VERBOSE_PREFIX_3  "%s", msg); break;
	case Q931_LOG_NOTICE: ast_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_WARNING: ast_log(__LOG_WARNING, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_ERR:
	case Q931_LOG_CRIT:
	case Q931_LOG_ALERT:
	case Q931_LOG_EMERG: ast_log(__LOG_ERROR, "libq931", 0, "", "%s", msg); break;
	}
}

void visdn_q931_timer_update(struct q931_lib *lib)
{
	pthread_kill(visdn_q931_thread, SIGURG);
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	visdn.libq931 = q931_init();
	q931_set_logger_func(visdn.libq931, visdn_logger);

	visdn.libq931->pvt = 
	visdn.libq931->timer_update = visdn_q931_timer_update;

	// Setup all callbacks for libq931 primitives
	visdn.libq931->alerting_indication =
		visdn_q931_alerting_indication;
	visdn.libq931->connect_indication =
		visdn_q931_connect_indication;
	visdn.libq931->disconnect_indication =
		visdn_q931_disconnect_indication;
	visdn.libq931->error_indication =
		visdn_q931_error_indication;
	visdn.libq931->info_indication =
		visdn_q931_info_indication;
	visdn.libq931->more_info_indication =
		visdn_q931_more_info_indication;
	visdn.libq931->notify_indication =
		visdn_q931_notify_indication;
	visdn.libq931->proceeding_indication =
		visdn_q931_proceeding_indication;
	visdn.libq931->progress_indication =
		visdn_q931_progress_indication;
	visdn.libq931->reject_indication =
		visdn_q931_reject_indication;
	visdn.libq931->release_confirm =
		visdn_q931_release_confirm;
	visdn.libq931->release_indication =
		visdn_q931_release_indication;
	visdn.libq931->resume_confirm =
		visdn_q931_resume_confirm;
	visdn.libq931->resume_indication =
		visdn_q931_resume_indication;
	visdn.libq931->setup_complete_indication =
		visdn_q931_setup_complete_indication;
	visdn.libq931->setup_confirm =
		visdn_q931_setup_confirm;
	visdn.libq931->setup_indication =
		visdn_q931_setup_indication;
	visdn.libq931->status_indication =
		visdn_q931_status_indication;
	visdn.libq931->suspend_confirm =
		visdn_q931_suspend_confirm;
	visdn.libq931->suspend_indication =
		visdn_q931_suspend_indication;
	visdn.libq931->timeout_indication =
		visdn_q931_timeout_indication;

	visdn.libq931->connect_channel =
		visdn_q931_connect_channel;
	visdn.libq931->disconnect_channel =
		visdn_q931_disconnect_channel;
	visdn.libq931->start_tone =
		visdn_q931_start_tone;
	visdn.libq931->stop_tone =
		visdn_q931_stop_tone;

	visdn.libq931->management_restart_confirm =
		visdn_q931_management_restart_confirm;
	visdn.libq931->timeout_management_indication =
		visdn_q931_timeout_management_indication;
	visdn.libq931->status_management_indication =
		visdn_q931_status_management_indication;

	visdn_reload_config();

	visdn.timer_fd = open("/dev/visdn/timer", O_RDONLY);
	if (visdn.timer_fd < 0) {
		ast_log(LOG_ERROR, "Unable to open timer: %s\n",
			strerror(errno));
		return -1;
	}

	visdn.control_fd = open("/dev/visdn/control", O_RDONLY);
	if (visdn.control_fd < 0) {
		ast_log(LOG_ERROR, "Unable to open control: %s\n",
			strerror(errno));
		return -1;
	}

	visdn.netlink_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if(visdn.netlink_socket < 0) {
		ast_log(LOG_ERROR, "Unable to open netlink socket: %s\n",
			strerror(errno));
		return -1;
	}

	struct sockaddr_nl snl;
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = RTMGRP_LINK;

	if (bind(visdn.netlink_socket,
			(struct sockaddr *)&snl,
			sizeof(snl)) < 0) {
		ast_log(LOG_ERROR, "Unable to bind netlink socket: %s\n",
			strerror(errno));
		return -1;
	}

	// Enum interfaces and open them
	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		ast_log(LOG_ERROR, "getifaddr: %s\n", strerror(errno));
		return -1;
	}

	int fd;
	fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		ast_log(LOG_ERROR, "socket: %s\n", strerror(errno));
		return -1;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifreq;
		memset(&ifreq, 0x00, sizeof(ifreq));
		strncpy(ifreq.ifr_name, ifaddr->ifa_name, sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			ast_log(LOG_ERROR, "ioctl (%s): %s\n",
				ifaddr->ifa_name, strerror(errno));
			return -1;
		}

		if (ifreq.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		if (!(ifaddr->ifa_flags & IFF_UP))
			continue;

		visdn_add_interface(ifreq.ifr_name);

	}
	close(fd);
	freeifaddrs(ifaddrs);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&visdn_q931_thread, &attr,
					visdn_q931_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start q931 thread.\n");
		return -1;
	}
	
	if (ast_channel_register_ex(VISDN_CHAN_TYPE, VISDN_DESCRIPTION, 
			 AST_FORMAT_ALAW,
			 visdn_request, visdn_devicestate)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VISDN_CHAN_TYPE);
		return -1;
	}
	
	ast_cli_register(&debug_visdn_generic);
	ast_cli_register(&no_debug_visdn_generic);
	ast_cli_register(&debug_visdn_q921);
	ast_cli_register(&no_debug_visdn_q921);
	ast_cli_register(&debug_visdn_q931);
	ast_cli_register(&no_debug_visdn_q931);
	ast_cli_register(&visdn_reload);
	ast_cli_register(&show_visdn_channels);
	ast_cli_register(&show_visdn_interfaces);
	ast_cli_register(&show_visdn_calls);
	
	return res;
}

int unload_module(void)
{
	ast_cli_unregister(&show_visdn_calls);
	ast_cli_unregister(&show_visdn_interfaces);
	ast_cli_unregister(&show_visdn_channels);
	ast_cli_unregister(&visdn_reload);
	ast_cli_unregister(&no_debug_visdn_q931);
	ast_cli_unregister(&debug_visdn_q931);
	ast_cli_unregister(&no_debug_visdn_q921);
	ast_cli_unregister(&debug_visdn_q921);
	ast_cli_unregister(&no_debug_visdn_generic);
	ast_cli_unregister(&debug_visdn_generic);

	ast_channel_unregister(VISDN_CHAN_TYPE);

	close(visdn.timer_fd);
	close(visdn.control_fd);

	if (visdn.libq931)
		q931_leave(visdn.libq931);

	return 0;
}

int reload(void)
{
	visdn_reload_config();

	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = visdn.usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return VISDN_DESCRIPTION;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

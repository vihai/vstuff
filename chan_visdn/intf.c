/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

//#include <asterisk/astmm.h>

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

#include <ifaddrs.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/version.h>

#include <linux/lapd.h>
#include <libq931/q931.h>

#include "chan_visdn.h"
#include "util.h"
#include "huntgroup.h"
#include "ton.h"

static struct visdn_interface *visdn_intf_alloc(void)
{
	struct visdn_interface *intf;

	intf = malloc(sizeof(*intf));
	if (!intf)
		return NULL;

	memset(intf, 0, sizeof(*intf));

	intf->refcnt = 1;

	INIT_LIST_HEAD(&intf->suspended_calls);
	INIT_LIST_HEAD(&intf->clip_numbers_list);
	intf->q931_intf = NULL;
	intf->configured = FALSE;
	intf->open_pending = FALSE;

	return intf;
}

struct visdn_interface *visdn_intf_get(struct visdn_interface *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);
	
	ast_mutex_lock(&visdn.usecnt_lock);
	intf->refcnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);

	return intf;
}

void visdn_intf_put(struct visdn_interface *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);

	ast_mutex_lock(&visdn.usecnt_lock);
	intf->refcnt--;

	if (!intf->refcnt) {
		free(intf);
	}
	ast_mutex_unlock(&visdn.usecnt_lock);
}

struct visdn_interface *visdn_intf_get_by_name(const char *name)
{
	struct visdn_interface *intf;

	ast_mutex_lock(&visdn.lock);
	
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (!strcasecmp(intf->name, name)) {
			ast_mutex_unlock(&visdn.lock);
			return visdn_intf_get(intf);
		}
	}

	ast_mutex_unlock(&visdn.lock);

	return NULL;
}

static enum q931_interface_network_role
	visdn_string_to_network_role(const char *str)
{
	if (!strcasecmp(str, "user"))
		return Q931_INTF_NET_USER;
	else if (!strcasecmp(str, "private"))
		return Q931_INTF_NET_PRIVATE;
	else if (!strcasecmp(str, "local"))
		return Q931_INTF_NET_LOCAL;
	else if (!strcasecmp(str, "transit"))
		return Q931_INTF_NET_TRANSIT;
	else if (!strcasecmp(str, "international"))
		return Q931_INTF_NET_INTERNATIONAL;
	else {
		ast_log(LOG_ERROR,
			"Unknown network_role '%s'\n",
			str);

		return Q931_INTF_NET_PRIVATE;
	}
}

static enum visdn_clir_mode
	visdn_string_to_clir_mode(const char *str)
{
	if (!strcasecmp(str, "off"))
		return VISDN_CLIR_MODE_OFF;
	else if (!strcasecmp(str, "default_off"))
		return VISDN_CLIR_MODE_DEFAULT_OFF;
	else if (!strcasecmp(str, "default_on"))
		return VISDN_CLIR_MODE_DEFAULT_ON;
	else if (!strcasecmp(str, "on"))
		return VISDN_CLIR_MODE_ON;
	else {
		ast_log(LOG_ERROR,
			"Unknown clir_mode '%s'\n",
			str);

		return VISDN_CLIR_MODE_DEFAULT_OFF;
	}
}

static void visdn_decode_clip_numbers(
	struct visdn_interface *intf,
	const char *value)
{
	char *str = strdup(value);
	char *strpos = str;
	char *tok;

	struct visdn_clip_number *num, *t;
	list_for_each_entry_safe(num, t,
		       &intf->clip_numbers_list, node) {
		list_del(&num->node);
		free(num);
	}

	while ((tok = strsep(&strpos, ","))) {
		while(*tok == ' ' || *tok == '\t')
			tok++;

		while(*(tok + strlen(tok) - 1) == ' ' ||
			*(tok + strlen(tok) - 1) == '\t')
			*(tok + strlen(tok) - 1) = '\0';

		struct visdn_clip_number *num;
		num = malloc(sizeof(*num));
		memset(num, 0, sizeof(num));

		strncpy(num->number, tok, sizeof(num->number));

		list_add_tail(&num->node, &intf->clip_numbers_list);
	}

	free(str);
}

static int visdn_intf_from_var(
	struct visdn_interface *intf,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "network_role")) {
		intf->network_role =
			visdn_string_to_network_role(var->value);
	} else if (!strcasecmp(var->name, "outbound_called_ton")) {
		intf->outbound_called_ton =
			visdn_ton_from_string(var->value);
	} else if (!strcasecmp(var->name, "force_outbound_cli")) {
		strncpy(intf->force_outbound_cli, var->value,
			sizeof(intf->force_outbound_cli));
	} else if (!strcasecmp(var->name, "force_outbound_cli_ton")) {
		if (!strlen(var->value) || !strcasecmp(var->value, "no"))
			intf->force_outbound_cli_ton =
				VISDN_TYPE_OF_NUMBER_UNSET;
		else
			intf->force_outbound_cli_ton =
				visdn_ton_from_string(var->value);
	} else if (!strcasecmp(var->name, "cli_rewriting")) {
		intf->cli_rewriting = ast_true(var->value);
	} else if (!strcasecmp(var->name, "national_prefix")) {
		strncpy(intf->national_prefix, var->value,
			sizeof(intf->national_prefix));
	} else if (!strcasecmp(var->name, "international_prefix")) {
		strncpy(intf->international_prefix, var->value,
			sizeof(intf->international_prefix));
	} else if (!strcasecmp(var->name, "network_specific_prefix")) {
		strncpy(intf->network_specific_prefix, var->value,
			sizeof(intf->network_specific_prefix));
	} else if (!strcasecmp(var->name, "subscriber_prefix")) {
		strncpy(intf->subscriber_prefix, var->value,
			sizeof(intf->subscriber_prefix));
	} else if (!strcasecmp(var->name, "abbreviated_prefix")) {
		strncpy(intf->abbreviated_prefix, var->value,
			sizeof(intf->abbreviated_prefix));
	} else if (!strcasecmp(var->name, "tones_option")) {
		intf->tones_option = ast_true(var->value);
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(intf->context, var->value, sizeof(intf->context));
	} else if (!strcasecmp(var->name, "clip_enabled")) {
		intf->clip_enabled = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clip_override")) {
		intf->clip_override = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clip_default_name")) {
		strncpy(intf->clip_default_name, var->value,
			sizeof(intf->clip_default_name));
	} else if (!strcasecmp(var->name, "clip_default_number")) {
		strncpy(intf->clip_default_number, var->value,
			sizeof(intf->clip_default_number));
	} else if (!strcasecmp(var->name, "clip_numbers")) {
		visdn_decode_clip_numbers(intf, var->value);
	} else if (!strcasecmp(var->name, "clip_special_arrangement")) {
		intf->clip_special_arrangement = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clir_mode")) {
		intf->clir_mode = visdn_string_to_clir_mode(var->value);
	} else if (!strcasecmp(var->name, "overlap_sending")) {
		intf->overlap_sending = ast_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_receiving")) {
		intf->overlap_receiving = ast_true(var->value);
	} else if (!strcasecmp(var->name, "call_bumping")) {
		intf->call_bumping = ast_true(var->value);
	} else if (!strcasecmp(var->name, "autorelease_dlc")) {
		intf->dlc_autorelease_time = atoi(var->value);
	} else if (!strcasecmp(var->name, "echocancel")) {
		intf->echocancel = ast_true(var->value);
	} else if (!strcasecmp(var->name, "echocancel_taps")) {
		intf->echocancel_taps = atoi(var->value);
	} else if (!strcasecmp(var->name, "t301")) {
		intf->T301 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t302")) {
		intf->T302 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t303")) {
		intf->T303 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t304")) {
		intf->T304 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t305")) {
		intf->T305 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t306")) {
		intf->T306 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t307")) {
		intf->T307 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t308")) {
		intf->T308 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t309")) {
		intf->T309 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t310")) {
		intf->T310 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t312")) {
		intf->T312 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t313")) {
		intf->T313 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t314")) {
		intf->T314 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t316")) {
		intf->T316 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t317")) {
		intf->T317 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t318")) {
		intf->T318 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t319")) {
		intf->T319 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t320")) {
		intf->T320 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t321")) {
		intf->T321 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t322")) {
		intf->T322 = atoi(var->value);
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
	dst->outbound_called_ton = src->outbound_called_ton;
	strcpy(dst->force_outbound_cli, src->force_outbound_cli);
	dst->force_outbound_cli_ton = src->force_outbound_cli_ton;
	dst->tones_option = src->tones_option;
	strcpy(dst->context, src->context);
	dst->clip_enabled = src->clip_enabled;
	dst->clip_override = src->clip_override;
	strcpy(dst->clip_default_name, src->clip_default_name);
	strcpy(dst->clip_default_number, src->clip_default_number);

	struct visdn_clip_number *num, *t;
	list_for_each_entry_safe(num, t,
		       &dst->clip_numbers_list, node) {
		list_del(&num->node);
		free(num);
	}

	list_for_each_entry(num, &src->clip_numbers_list, node) {

		struct visdn_clip_number *num2;

		num2 = malloc(sizeof(*num2));
		strcpy(num2->number, num->number);
		list_add_tail(&num2->node, &dst->clip_numbers_list);
	}
	
	dst->clip_special_arrangement = src->clip_special_arrangement;
	dst->clir_mode = src->clir_mode;
	dst->overlap_sending = src->overlap_sending;
	dst->overlap_receiving = src->overlap_receiving;
	dst->call_bumping = src->call_bumping;
	dst->cli_rewriting = src->cli_rewriting;
	strcpy(dst->national_prefix, src->national_prefix);
	strcpy(dst->international_prefix, src->international_prefix);
	strcpy(dst->network_specific_prefix, src->network_specific_prefix);
	strcpy(dst->subscriber_prefix, src->subscriber_prefix);
	strcpy(dst->abbreviated_prefix, src->abbreviated_prefix);
	dst->dlc_autorelease_time = src->dlc_autorelease_time;
	dst->echocancel = src->echocancel;
	dst->echocancel_taps = src->echocancel_taps;
	dst->T301 = src->T301;
	dst->T302 = src->T302;
	dst->T303 = src->T303;
	dst->T304 = src->T304;
	dst->T305 = src->T305;
	dst->T306 = src->T306;
	dst->T307 = src->T307;
	dst->T308 = src->T308;
	dst->T309 = src->T309;
	dst->T310 = src->T310;
	dst->T312 = src->T312;
	dst->T313 = src->T313;
	dst->T314 = src->T314;
	dst->T316 = src->T316;
	dst->T317 = src->T317;
	dst->T318 = src->T318;
	dst->T319 = src->T319;
	dst->T320 = src->T320;
	dst->T321 = src->T321;
	dst->T322 = src->T322;
}

int visdn_intf_clip_valid(
	struct visdn_interface *intf,
	const char *called_number)
{
	struct visdn_clip_number *num;
	list_for_each_entry(num, &intf->clip_numbers_list, node) {
		if (ast_extension_match(num->number, called_number))
			return TRUE;
	}

	return FALSE;
}

int visdn_intf_open(struct visdn_interface *intf)
{
	assert(!intf->q931_intf);

	intf->open_pending = TRUE;

	intf->q931_intf = q931_open_interface(intf->name, 0);
	if (!intf->q931_intf) {
		ast_log(LOG_WARNING,
			"Cannot open interface %s, skipping\n",
			intf->name);

		return -1;
	}

	intf->q931_intf->pvt = intf;
	intf->q931_intf->network_role = intf->network_role;
	intf->q931_intf->dlc_autorelease_time = intf->dlc_autorelease_time;
	intf->q931_intf->enable_bumping = intf->call_bumping;

	if (intf->T301) intf->q931_intf->T301 = intf->T301 * 1000000LL;
	if (intf->T302) intf->q931_intf->T302 = intf->T302 * 1000000LL;
	if (intf->T303) intf->q931_intf->T303 = intf->T303 * 1000000LL;
	if (intf->T304) intf->q931_intf->T304 = intf->T304 * 1000000LL;
	if (intf->T305) intf->q931_intf->T305 = intf->T305 * 1000000LL;
	if (intf->T306) intf->q931_intf->T306 = intf->T306 * 1000000LL;
	if (intf->T308) intf->q931_intf->T308 = intf->T308 * 1000000LL;
	if (intf->T309) intf->q931_intf->T309 = intf->T309 * 1000000LL;
	if (intf->T310) intf->q931_intf->T310 = intf->T310 * 1000000LL;
	if (intf->T312) intf->q931_intf->T312 = intf->T312 * 1000000LL;
	if (intf->T313) intf->q931_intf->T313 = intf->T313 * 1000000LL;
	if (intf->T314) intf->q931_intf->T314 = intf->T314 * 1000000LL;
	if (intf->T316) intf->q931_intf->T316 = intf->T316 * 1000000LL;
	if (intf->T317) intf->q931_intf->T317 = intf->T317 * 1000000LL;
	if (intf->T318) intf->q931_intf->T318 = intf->T318 * 1000000LL;
	if (intf->T319) intf->q931_intf->T319 = intf->T319 * 1000000LL;
	if (intf->T320) intf->q931_intf->T320 = intf->T320 * 1000000LL;
	if (intf->T321) intf->q931_intf->T321 = intf->T321 * 1000000LL;
	if (intf->T322) intf->q931_intf->T322 = intf->T322 * 1000000LL;

	if (intf->q931_intf->role == LAPD_ROLE_NT) {
		if (listen(intf->q931_intf->master_socket, 100) < 0) {
			ast_log(LOG_ERROR,
				"cannot listen on master socket: %s\n",
				strerror(errno));

			return -1;
		}
	}

	char path[PATH_MAX];
	char goodpath[PATH_MAX];
	snprintf(path, sizeof(path),
		"/sys/class/net/%s/visdn_channel/leg_a/",
		intf->name);

	struct stat st;
	for(;;) {
		if (stat(path, &st) < 0) {
			if (errno == ENOENT)
				break;

			ast_log(LOG_ERROR,
				"cannot stat(%s): %s\n",
				path,
				strerror(errno));

			return -1;
		}

		strcpy(goodpath, path);

		strncat(path, "connected/other_leg/", sizeof(path));
	}

	strncat(goodpath, "../..", sizeof(goodpath));

	if (realpath(goodpath, intf->remote_port) < 0) {
		ast_log(LOG_ERROR,
			"cannot find realpath(%s): %s\n",
			goodpath,
			strerror(errno));
	}

	intf->open_pending = FALSE;

	if (intf->q931_intf->role == LAPD_ROLE_NT) {
		if (list_empty(&intf->clip_numbers_list)) {
			ast_log(LOG_NOTICE,
				"Interface '%s' is configured in network"
				" mode but clip_numbers is empty\n",
				intf->name);
		} else if (!strlen(intf->clip_default_number)) {
			ast_log(LOG_NOTICE,
				"Interface '%s' is configured in network"
				" mode but clip_default_number is empty\n",
				intf->name);
		} else if (!visdn_intf_clip_valid(intf,
					intf->clip_default_number)) {

			ast_log(LOG_NOTICE,
				"Interface '%s' clip_numbers should contain "
				"clip_default_number (%s)\n",
				intf->name,
				intf->clip_default_number);
		}
	}

	return 0;
}

static void visdn_intf_reconfigure(
	struct ast_config *cfg,
	const char *name)
{
	/* Allocate a new interface */
	
	struct visdn_interface *intf;
	intf = visdn_intf_alloc();
	if (!intf)
		return;

	/* Configure it with default configuration */
	strncpy(intf->name, name, sizeof(intf->name));
	visdn_copy_interface_config(intf, &visdn.default_intf);

	/* Now read the configuration from file */
	intf->configured = TRUE;

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)name);
	while (var) {
		if (visdn_intf_from_var(intf, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration "
				"variable %s\n",
				var->name);
		}

		var = var->next;
	}

	ast_mutex_lock(&visdn.lock);

	/* Now do the switch thing. If there is another interface, * unlink it
	 * from the list and drop the reference. Other references will continue
	 * to be valid. */
	struct visdn_interface *old_intf, *tpos;
	list_for_each_entry_safe(old_intf, tpos, &visdn.ifs, ifs_node) {
		if (!strcasecmp(old_intf->name, name)) {
			intf->q931_intf = old_intf->q931_intf;

			list_del(&old_intf->ifs_node);

			break;
		}
	}

	if (!intf->q931_intf) {
		visdn_debug("Opening interface %s\n", name);

		visdn_intf_open(intf);
		refresh_polls_list();
	}

	list_add_tail(&visdn_intf_get(intf)->ifs_node, &visdn.ifs);

	ast_mutex_unlock(&visdn.lock);
}

void visdn_intf_reload(struct ast_config *cfg)
{
	ast_mutex_lock(&visdn.lock);

	/* Read default interface configuration */
	struct ast_variable *var;
	var = ast_variable_browse(cfg, "global");
	while (var) {
		if (visdn_intf_from_var(&visdn.default_intf, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	{
	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		intf->configured = FALSE;
	}
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general") ||
		    !strcasecmp(cat, "global") ||
		    !strncasecmp(cat, VISDN_HUNTGROUP_PREFIX,
				strlen(VISDN_HUNTGROUP_PREFIX)))
			continue;

		visdn_intf_reconfigure(cfg, cat);
	}

	ast_mutex_unlock(&visdn.lock);
}

/*---------------------------------------------------------------------------*/

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

	return "*UNKNOWN*";
}

static const char *visdn_clir_mode_to_text(
	enum visdn_clir_mode mode)
{
	switch(mode) {
	case VISDN_CLIR_MODE_OFF:
		return "Off";
	case VISDN_CLIR_MODE_DEFAULT_OFF:
		return "Default off";
	case VISDN_CLIR_MODE_DEFAULT_ON:
		return "Default on";
	case VISDN_CLIR_MODE_ON:
		return "On";
	}

	return "*UNKNOWN*";
}

static int do_show_visdn_interfaces(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&visdn.lock);

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {

		ast_cli(fd, "\n-- %s --\n", intf->name);

		ast_cli(fd, "Role                      : %s\n",
				intf->q931_intf ?
					(intf->q931_intf->role == LAPD_ROLE_NT ?
						"NT" : "TE") :
					"UNUSED!");

		ast_cli(fd,
			"Network role              : %s\n"
			"Outbound type of number   : %s\n"
			"Forced CLI                : %s\n"
			"Forced CLI type of number : %s\n"
			"Tones option              : %s\n"
			"Echo canceller            : %s\n"
			"Echo canceller taps       : %d (%d ms)\n"
			"Context                   : %s\n"
			"Overlap Sending           : %s\n"
			"Overlap Receiving         : %s\n"
			"Call bumping              : %s\n"
			"CLI rewriting             : %s\n"
			"National prefix           : %s\n"
			"International prefix      : %s\n"
			"Newtork specific prefix   : %s\n"
			"Subscriber prefix         : %s\n"
			"Abbreviated prefix        : %s\n"
			"Autorelease time          : %d\n"
			"CLIR mode                 : %s\n"
			"CLIP enabled              : %s\n"
			"CLIP override             : %s\n"
			"CLIP default              : %s <%s>\n"
			"CLIP special arrangement  : %s\n",
			visdn_interface_network_role_to_string(
				intf->network_role),
			visdn_ton_to_string(
				intf->outbound_called_ton),
			intf->force_outbound_cli,
			visdn_ton_to_string(
				intf->force_outbound_cli_ton),
			intf->tones_option ? "Yes" : "No",
			intf->echocancel ? "Yes" : "No",
			intf->echocancel_taps, intf->echocancel_taps / 8,
			intf->context,
			intf->overlap_sending ? "Yes" : "No",
			intf->overlap_receiving ? "Yes" : "No",
			intf->call_bumping ? "Yes" : "No",
			intf->cli_rewriting ? "Yes" : "No",
			intf->national_prefix,
			intf->international_prefix,
			intf->network_specific_prefix,
			intf->subscriber_prefix,
			intf->abbreviated_prefix,
			intf->dlc_autorelease_time,
			visdn_clir_mode_to_text(intf->clir_mode),
			intf->clip_enabled ? "Yes" : "No",
			intf->clip_override ? "Yes" : "No",
			intf->clip_default_name,
			intf->clip_default_number,
			intf->clip_special_arrangement ? "Yes" : "No");

		ast_cli(fd, "CLIP Numbers              : ");
		struct visdn_clip_number *num;
		list_for_each_entry(num, &intf->clip_numbers_list, node) {
			ast_cli(fd, "%s ", num->number);
		}
		ast_cli(fd, "\n");

		if (intf->q931_intf) {
			if (intf->q931_intf->role == LAPD_ROLE_NT) {
				ast_cli(fd, "DLCs                      : ");

				struct q931_dlc *dlc;
				list_for_each_entry(dlc, &intf->q931_intf->dlcs,
						intf_node) {
					ast_cli(fd, "%d ", dlc->tei);

				}

				ast_cli(fd, "\n");

#define TIMER_CONFIG(timer)					\
	ast_cli(fd, #timer ": %3lld%-5s",			\
		intf->q931_intf->timer / 1000000LL,		\
		intf->timer ? " (*)" : "");

#define TIMER_CONFIG_LN(timer)					\
	ast_cli(fd, #timer ": %3lld%-5s\n",			\
		intf->q931_intf->timer / 1000000LL,		\
		intf->timer ? " (*)" : "");

				TIMER_CONFIG(T301);
				TIMER_CONFIG(T301);
				TIMER_CONFIG(T302);
				TIMER_CONFIG_LN(T303);
				TIMER_CONFIG(T304);
				TIMER_CONFIG(T305);
				TIMER_CONFIG(T306);
				ast_cli(fd, "T307: %d\n", intf->T307);
				TIMER_CONFIG(T308);
				TIMER_CONFIG(T309);
				TIMER_CONFIG(T310);
				TIMER_CONFIG_LN(T312);
				TIMER_CONFIG(T314);
				TIMER_CONFIG(T316);
				TIMER_CONFIG(T317);
				TIMER_CONFIG_LN(T320);
				TIMER_CONFIG(T321);
				TIMER_CONFIG_LN(T322);
			} else {
				TIMER_CONFIG(T301);
				TIMER_CONFIG(T302);
				TIMER_CONFIG(T303);
				TIMER_CONFIG_LN(T304);
				TIMER_CONFIG(T305);
				TIMER_CONFIG(T306);
				ast_cli(fd, "T307: %d\n", intf->T307);
				TIMER_CONFIG_LN(T308);
				TIMER_CONFIG(T309);
				TIMER_CONFIG(T310);
				TIMER_CONFIG(T312);
				TIMER_CONFIG_LN(T313);
				TIMER_CONFIG(T314);
				TIMER_CONFIG(T316);
				TIMER_CONFIG(T317);
				TIMER_CONFIG(T318);
				TIMER_CONFIG_LN(T319);
				TIMER_CONFIG(T320);
				TIMER_CONFIG(T321);
				TIMER_CONFIG_LN(T322);
			}

		}

		ast_cli(fd, "Parked calls:\n");
		struct visdn_suspended_call *suspended_call;
		list_for_each_entry(suspended_call, &intf->suspended_calls,
			       					node) {

			char sane_str[10];
			char hex_str[20];
			int i;
			for(i=0;
			    i<sizeof(sane_str) &&
				i<suspended_call->call_identity_len;
			    i++) {
				sane_str[i] =
				isprint(suspended_call->call_identity[i]) ?
					suspended_call->call_identity[i] : '.';

				snprintf(hex_str + (i*2),
					sizeof(hex_str)-(i*2),
					"%02x ",
					suspended_call->call_identity[i]);
			}
			sane_str[i] = '\0';
			hex_str[i*2] = '\0';

			ast_cli(fd, "    %s (%s)\n",
				sane_str,
				hex_str);
		}
	}

	ast_mutex_unlock(&visdn.lock);

	return 0;
}

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

/*---------------------------------------------------------------------------*/

void visdn_intf_cli_register(void)
{
	ast_cli_register(&show_visdn_interfaces);
}

void visdn_intf_cli_unregister(void)
{
	ast_cli_unregister(&show_visdn_interfaces);
}

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
#include <libq931/intf.h>

#include "chan_visdn.h"
#include "util.h"
#include "huntgroup.h"
#include "ton.h"
#include "numbers_list.h"

struct visdn_intf *visdn_intf_alloc(void)
{
	struct visdn_intf *intf;

	intf = malloc(sizeof(*intf));
	if (!intf)
		return NULL;

	memset(intf, 0, sizeof(*intf));

	intf->refcnt = 1;

	intf->status = VISDN_INTF_STATUS_UNINITIALIZED;

	intf->q931_intf = NULL;
	intf->configured = FALSE;
	intf->open_pending = FALSE;

	INIT_LIST_HEAD(&intf->suspended_calls);

	return intf;
}

struct visdn_intf *visdn_intf_get(struct visdn_intf *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);
	
	ast_mutex_lock(&visdn.usecnt_lock);
	intf->refcnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);

	return intf;
}

void visdn_intf_put(struct visdn_intf *intf)
{
	assert(intf);
	assert(intf->refcnt > 0);

	ast_mutex_lock(&visdn.usecnt_lock);
	intf->refcnt--;

	if (!intf->refcnt)
		free(intf);

	ast_mutex_unlock(&visdn.usecnt_lock);
}

struct visdn_intf *visdn_intf_get_by_name(const char *name)
{
	struct visdn_intf *intf;

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

struct visdn_ic *visdn_ic_alloc(void)
{
	struct visdn_ic *ic;

	ic = malloc(sizeof(*ic));
	if (!ic)
		return NULL;

	memset(ic, 0, sizeof(*ic));

	ic->refcnt = 1;

	INIT_LIST_HEAD(&ic->clip_numbers_list);
	INIT_LIST_HEAD(&ic->trans_numbers_list);

	return ic;
}

struct visdn_ic *visdn_ic_get(struct visdn_ic *ic)
{
	assert(ic);
	assert(ic->refcnt > 0);
	
	ast_mutex_lock(&visdn.usecnt_lock);
	ic->refcnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);

	return ic;
}

void visdn_ic_put(struct visdn_ic *ic)
{
	assert(ic);
	assert(ic->refcnt > 0);

	ast_mutex_lock(&visdn.usecnt_lock);
	ic->refcnt--;

	if (!ic->refcnt) {
		if (ic->intf)
			visdn_intf_put(ic->intf);

		free(ic);
	}

	ast_mutex_unlock(&visdn.usecnt_lock);
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
	if (!strcasecmp(str, "unrestricted"))
		return VISDN_CLIR_MODE_UNRESTRICTED;
	else if (!strcasecmp(str, "unrestricted_default"))
		return VISDN_CLIR_MODE_UNRESTRICTED_DEFAULT;
	else if (!strcasecmp(str, "restricted_default"))
		return VISDN_CLIR_MODE_RESTRICTED_DEFAULT;
	else if (!strcasecmp(str, "restricted"))
		return VISDN_CLIR_MODE_RESTRICTED;
	else {
		ast_log(LOG_ERROR,
			"Unknown clir_mode '%s'\n",
			str);

		return VISDN_CLIR_MODE_UNRESTRICTED_DEFAULT;
	}
}

static int visdn_ic_from_var(
	struct visdn_ic *ic,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "tei")) {
		if (!strcasecmp(var->value, "dynamic"))
			ic->tei = LAPD_DYNAMIC_TEI;
		else
			ic->tei = atoi(var->value);
	} else if (!strcasecmp(var->name, "network_role")) {
		ic->network_role =
			visdn_string_to_network_role(var->value);
	} else if (!strcasecmp(var->name, "outbound_called_ton")) {
		ic->outbound_called_ton =
			visdn_ton_from_string(var->value);
	} else if (!strcasecmp(var->name, "force_outbound_cli")) {
		strncpy(ic->force_outbound_cli, var->value,
			sizeof(ic->force_outbound_cli));
	} else if (!strcasecmp(var->name, "force_outbound_cli_ton")) {
		if (!strlen(var->value) || !strcasecmp(var->value, "no"))
			ic->force_outbound_cli_ton =
				VISDN_TYPE_OF_NUMBER_UNSET;
		else
			ic->force_outbound_cli_ton =
				visdn_ton_from_string(var->value);
	} else if (!strcasecmp(var->name, "cli_rewriting")) {
		ic->cli_rewriting = ast_true(var->value);
	} else if (!strcasecmp(var->name, "national_prefix")) {
		strncpy(ic->national_prefix, var->value,
			sizeof(ic->national_prefix));
	} else if (!strcasecmp(var->name, "international_prefix")) {
		strncpy(ic->international_prefix, var->value,
			sizeof(ic->international_prefix));
	} else if (!strcasecmp(var->name, "network_specific_prefix")) {
		strncpy(ic->network_specific_prefix, var->value,
			sizeof(ic->network_specific_prefix));
	} else if (!strcasecmp(var->name, "subscriber_prefix")) {
		strncpy(ic->subscriber_prefix, var->value,
			sizeof(ic->subscriber_prefix));
	} else if (!strcasecmp(var->name, "abbreviated_prefix")) {
		strncpy(ic->abbreviated_prefix, var->value,
			sizeof(ic->abbreviated_prefix));
	} else if (!strcasecmp(var->name, "tones_option")) {
		ic->tones_option = ast_true(var->value);
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(ic->context, var->value, sizeof(ic->context));
	} else if (!strcasecmp(var->name, "language")) {
		strncpy(ic->language, var->value, sizeof(ic->language));
	} else if (!strcasecmp(var->name, "trans_numbers")) {
		visdn_numbers_list_from_string(
			&ic->trans_numbers_list, var->value);
	} else if (!strcasecmp(var->name, "clip_enabled")) {
		ic->clip_enabled = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clip_override")) {
		ic->clip_override = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clip_default_name")) {
		strncpy(ic->clip_default_name, var->value,
			sizeof(ic->clip_default_name));
	} else if (!strcasecmp(var->name, "clip_default_number")) {
		strncpy(ic->clip_default_number, var->value,
			sizeof(ic->clip_default_number));
	} else if (!strcasecmp(var->name, "clip_numbers")) {
		visdn_numbers_list_from_string(
			&ic->clip_numbers_list, var->value);
	} else if (!strcasecmp(var->name, "clip_special_arrangement")) {
		ic->clip_special_arrangement = ast_true(var->value);
	} else if (!strcasecmp(var->name, "clir_mode")) {
		ic->clir_mode = visdn_string_to_clir_mode(var->value);
	} else if (!strcasecmp(var->name, "overlap_sending")) {
		ic->overlap_sending = ast_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_receiving")) {
		ic->overlap_receiving = ast_true(var->value);
	} else if (!strcasecmp(var->name, "call_bumping")) {
		ic->call_bumping = ast_true(var->value);
	} else if (!strcasecmp(var->name, "autorelease_dlc")) {
		ic->dlc_autorelease_time = atoi(var->value);
	} else if (!strcasecmp(var->name, "echocancel")) {
		ic->echocancel = ast_true(var->value);
	} else if (!strcasecmp(var->name, "echocancel_taps")) {
		ic->echocancel_taps = atoi(var->value);
	} else if (!strcasecmp(var->name, "t301")) {
		ic->T301 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t302")) {
		ic->T302 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t303")) {
		ic->T303 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t304")) {
		ic->T304 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t305")) {
		ic->T305 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t306")) {
		ic->T306 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t307")) {
		ic->T307 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t308")) {
		ic->T308 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t309")) {
		ic->T309 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t310")) {
		ic->T310 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t312")) {
		ic->T312 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t313")) {
		ic->T313 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t314")) {
		ic->T314 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t316")) {
		ic->T316 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t317")) {
		ic->T317 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t318")) {
		ic->T318 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t319")) {
		ic->T319 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t320")) {
		ic->T320 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t321")) {
		ic->T321 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t322")) {
		ic->T322 = atoi(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void visdn_ic_copy(
	struct visdn_ic *dst,
	const struct visdn_ic *src)
{
	dst->tei = src->tei;
	dst->network_role = src->network_role;
	dst->outbound_called_ton = src->outbound_called_ton;
	strcpy(dst->force_outbound_cli, src->force_outbound_cli);
	dst->force_outbound_cli_ton = src->force_outbound_cli_ton;
	dst->tones_option = src->tones_option;
	strcpy(dst->context, src->context);
	strcpy(dst->language, src->language);
	dst->clip_enabled = src->clip_enabled;
	dst->clip_override = src->clip_override;
	strcpy(dst->clip_default_name, src->clip_default_name);
	strcpy(dst->clip_default_number, src->clip_default_number);

	visdn_numbers_list_copy(&dst->trans_numbers_list,
			&src->trans_numbers_list);
	visdn_numbers_list_copy(&dst->clip_numbers_list,
			&src->clip_numbers_list);

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

int visdn_intf_open(struct visdn_intf *intf, struct visdn_ic *ic)
{
	assert(!intf->q931_intf);

	intf->open_pending = TRUE;

	intf->q931_intf = q931_intf_open(intf->name, 0, ic->tei);
	if (!intf->q931_intf) {
		ast_log(LOG_WARNING,
			"Cannot open interface %s, skipping\n",
			intf->name);

		goto err_intf_open;
	}

	intf->q931_intf->pvt = intf;
	intf->q931_intf->network_role = ic->network_role;
	intf->q931_intf->dlc_autorelease_time = ic->dlc_autorelease_time;
	intf->q931_intf->enable_bumping = ic->call_bumping;

	intf->mgmt_fd = socket(PF_LAPD, SOCK_SEQPACKET, LAPD_SAPI_MGMT);
	if (intf->mgmt_fd < 0) {
		ast_log(LOG_WARNING,
			"Cannot open management socket: %s\n",
			strerror(errno));

		goto err_socket;
	}

	if (setsockopt(intf->mgmt_fd, SOL_LAPD, SO_BINDTODEVICE, intf->name,
					strlen(intf->name) + 1) < 0) {

		ast_log(LOG_WARNING,
			"Cannot bind management socket to %s: %s, skipping\n",
			strerror(errno),
			intf->name);

		goto err_setsockopt;
	}

	int oldflags;
	oldflags = fcntl(intf->mgmt_fd, F_GETFL, 0);
	if (oldflags < 0) {
		ast_log(LOG_WARNING,
			"%s: fcntl(GETFL): %s\n",
			intf->name,
			strerror(errno));
		goto err_fcntl_getfl;
	}

	if (fcntl(intf->mgmt_fd, F_SETFL, oldflags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING,
			"fcntl(F_SETFL): %s\n",
			strerror(errno));

		goto err_fcntl_setfl;
	}

	if (ic->T301) intf->q931_intf->T301 = ic->T301 * 1000000LL;
	if (ic->T302) intf->q931_intf->T302 = ic->T302 * 1000000LL;
	if (ic->T303) intf->q931_intf->T303 = ic->T303 * 1000000LL;
	if (ic->T304) intf->q931_intf->T304 = ic->T304 * 1000000LL;
	if (ic->T305) intf->q931_intf->T305 = ic->T305 * 1000000LL;
	if (ic->T306) intf->q931_intf->T306 = ic->T306 * 1000000LL;
	if (ic->T308) intf->q931_intf->T308 = ic->T308 * 1000000LL;
	if (ic->T309) intf->q931_intf->T309 = ic->T309 * 1000000LL;
	if (ic->T310) intf->q931_intf->T310 = ic->T310 * 1000000LL;
	if (ic->T312) intf->q931_intf->T312 = ic->T312 * 1000000LL;
	if (ic->T313) intf->q931_intf->T313 = ic->T313 * 1000000LL;
	if (ic->T314) intf->q931_intf->T314 = ic->T314 * 1000000LL;
	if (ic->T316) intf->q931_intf->T316 = ic->T316 * 1000000LL;
	if (ic->T317) intf->q931_intf->T317 = ic->T317 * 1000000LL;
	if (ic->T318) intf->q931_intf->T318 = ic->T318 * 1000000LL;
	if (ic->T319) intf->q931_intf->T319 = ic->T319 * 1000000LL;
	if (ic->T320) intf->q931_intf->T320 = ic->T320 * 1000000LL;
	if (ic->T321) intf->q931_intf->T321 = ic->T321 * 1000000LL;
	if (ic->T322) intf->q931_intf->T322 = ic->T322 * 1000000LL;

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

			goto err_stat;
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

		goto err_realpath;
	}

	intf->open_pending = FALSE;

	if (intf->q931_intf->role == LAPD_INTF_ROLE_NT) {
		if (list_empty(&ic->clip_numbers_list)) {
			ast_log(LOG_NOTICE,
				"Interface '%s' is configured in network"
				" mode but clip_numbers is empty\n",
				intf->name);
		} else if (!strlen(ic->clip_default_number)) {
			ast_log(LOG_NOTICE,
				"Interface '%s' is configured in network"
				" mode but clip_default_number is empty\n",
				intf->name);
		} else if (!visdn_numbers_list_match(&ic->clip_numbers_list,
					ic->clip_default_number)) {

			ast_log(LOG_NOTICE,
				"Interface '%s' clip_numbers should contain "
				"clip_default_number (%s)\n",
				intf->name,
				ic->clip_default_number);
		}
	}

	intf->status = VISDN_INTF_STATUS_ONLINE;

	return 0;

err_realpath:
err_stat:
err_fcntl_setfl:
err_fcntl_getfl:
err_setsockopt:
	close(intf->mgmt_fd);
err_socket:
	q931_intf_close(intf->q931_intf);
err_intf_open:

	intf->status = VISDN_INTF_STATUS_FAILED;

	return -1;
}

void visdn_intf_close(struct visdn_intf *intf)
{
	close(intf->mgmt_fd);

	q931_intf_close(intf->q931_intf);
	intf->q931_intf = NULL;
}

void visdn_ic_setdefault(struct visdn_ic *ic)
{
	ic->tei = LAPD_DYNAMIC_TEI;
	ic->network_role = Q931_INTF_NET_PRIVATE;
	ic->outbound_called_ton = VISDN_TYPE_OF_NUMBER_UNKNOWN;
	strcpy(ic->force_outbound_cli, "");
	ic->force_outbound_cli_ton = VISDN_TYPE_OF_NUMBER_UNSET;
	ic->tones_option = TRUE;
	strcpy(ic->context, "visdn");
	strcpy(ic->language, "");
	ic->clip_enabled = TRUE;
	ic->clip_override = FALSE;
	strcpy(ic->clip_default_name, "");
	strcpy(ic->clip_default_number, "");
	ic->clip_special_arrangement = FALSE;
	ic->clir_mode = VISDN_CLIR_MODE_UNRESTRICTED_DEFAULT;
	ic->overlap_sending = TRUE;
	ic->overlap_receiving = FALSE;
	ic->call_bumping = FALSE;
	ic->cli_rewriting = FALSE;
	strcpy(ic->national_prefix, "");
	strcpy(ic->international_prefix, "");
	strcpy(ic->network_specific_prefix, "");
	strcpy(ic->subscriber_prefix, "");
	strcpy(ic->abbreviated_prefix, "");
	ic->dlc_autorelease_time = 10;
	ic->echocancel = FALSE;
	ic->echocancel_taps = 256;
	ic->T307 = 180;
}

static void visdn_intf_reconfigure(
	struct ast_config *cfg,
	const char *name)
{
	/* Allocate a new interface */
	
	struct visdn_ic *ic;
	ic = visdn_ic_alloc();
	if (!ic)
		return;

	visdn_ic_copy(ic, visdn.default_ic);

	/* Now read the configuration from file */
//	intf->configured = TRUE;

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)name);
	while (var) {
		if (visdn_ic_from_var(ic, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration "
				"variable %s\n",
				var->name);
		}

		var = var->next;
	}

	ast_mutex_lock(&visdn.lock);

	struct visdn_intf *intf = visdn_intf_get_by_name(name);
	if (!intf) {
		intf = visdn_intf_alloc();
		if (!intf) {
			ast_mutex_unlock(&visdn.lock);
			return;
		}

		strncpy(intf->name, name, sizeof(intf->name));

		visdn_debug("Opening interface %s\n", name);

		visdn_intf_open(intf, ic);
		refresh_polls_list();

		list_add_tail(&intf->ifs_node, &visdn.ifs);
	}

	if (intf->current_ic)
		visdn_ic_put(intf->current_ic);

	ic->intf = intf;

	intf->current_ic = visdn_ic_get(ic);

	ast_mutex_unlock(&visdn.lock);
}

void visdn_intf_reload(struct ast_config *cfg)
{
	ast_mutex_lock(&visdn.lock);

	/* Read default interface configuration */
	struct ast_variable *var;
	var = ast_variable_browse(cfg, "global");
	while (var) {
		if (visdn_ic_from_var(visdn.default_ic, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	{
	struct visdn_intf *intf;
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

static const char *visdn_intf_mode_to_string(
	enum lapd_intf_mode value)
{
	switch(value) {
	case LAPD_INTF_MODE_POINT_TO_POINT:
		return "Point-to-point";
	case LAPD_INTF_MODE_MULTIPOINT:
		return "Point-to-Multipoint";
	}

	return "*UNKNOWN*";
}

static const char *visdn_intf_mode_to_string_short(
	enum lapd_intf_mode value)
{
	switch(value) {
	case LAPD_INTF_MODE_POINT_TO_POINT:
		return "P2P";
	case LAPD_INTF_MODE_MULTIPOINT:
		return "P2MP";
	}

	return "*UNKNOWN*";
}

static const char *visdn_ic_network_role_to_string(
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
	case VISDN_CLIR_MODE_UNRESTRICTED:
		return "Unrestricted";
	case VISDN_CLIR_MODE_UNRESTRICTED_DEFAULT:
		return "Unrestricted by default";
	case VISDN_CLIR_MODE_RESTRICTED_DEFAULT:
		return "Restricted by default";
	case VISDN_CLIR_MODE_RESTRICTED:
		return "Restricted";
	}

	return "*UNKNOWN*";
}

static const char *visdn_intf_status_to_text(enum visdn_intf_status status)
{
	switch(status) {
	case VISDN_INTF_STATUS_UNINITIALIZED:
		return "Uninitialized";
	case VISDN_INTF_STATUS_OFFLINE:
		return "OFFLINE";
	case VISDN_INTF_STATUS_ONLINE:
		return "Online";
	case VISDN_INTF_STATUS_FAILED:
		return "FAILED!";
	}

	return "*UNKNOWN*";
}

static void visdn_print_intf_details(int fd, struct visdn_intf *intf)
{
	struct visdn_ic *ic = intf->current_ic;

	ast_cli(fd, "\n-- %s --\n", intf->name);
		ast_cli(fd, "Status                    : %s\n",
			visdn_intf_status_to_text(intf->status));

	if (intf->q931_intf) {
		ast_cli(fd, "Role                      : %s\n",
			intf->q931_intf->role == LAPD_INTF_ROLE_NT ?
				"NT" : "TE");

		ast_cli(fd,
			"Mode                      : %s\n",
			visdn_intf_mode_to_string(
				intf->q931_intf->mode));

		if (intf->q931_intf->tei != LAPD_DYNAMIC_TEI)
			ast_cli(fd, "TEI                       : %d\n",
				intf->q931_intf->tei);
		else
			ast_cli(fd, "TEI                       : "
					"Dynamic\n");
	}

	ast_cli(fd,
		"Network role              : %s\n"
		"Context                   : %s\n"
		"Language                  : %s\n",
		visdn_ic_network_role_to_string(
			ic->network_role),
		ic->context,
		ic->language);

	ast_cli(fd, "Transparent Numbers       : ");
	{
	struct visdn_number *num;
	list_for_each_entry(num, &ic->clip_numbers_list, node) {
		ast_cli(fd, "%s ", num->number);
	}
	}
	ast_cli(fd, "\n");

	ast_cli(fd,
		"Echo canceller            : %s\n"
		"Echo canceller taps       : %d (%d ms)\n"
		"Tones option              : %s\n"
		"Overlap Sending           : %s\n"
		"Overlap Receiving         : %s\n"
		"Call bumping              : %s\n",
		ic->echocancel ? "Yes" : "No",
		ic->echocancel_taps, ic->echocancel_taps / 8,
		ic->tones_option ? "Yes" : "No",
		ic->overlap_sending ? "Yes" : "No",
		ic->overlap_receiving ? "Yes" : "No",
		ic->call_bumping ? "Yes" : "No");

	ast_cli(fd,
		"National prefix           : %s\n"
		"International prefix      : %s\n"
		"Newtork specific prefix   : %s\n"
		"Subscriber prefix         : %s\n"
		"Abbreviated prefix        : %s\n"
		"Autorelease time          : %d\n\n",
		ic->national_prefix,
		ic->international_prefix,
		ic->network_specific_prefix,
		ic->subscriber_prefix,
		ic->abbreviated_prefix,
		ic->dlc_autorelease_time);

	ast_cli(fd,
		"Outbound type of number   : %s\n\n"
		"Forced CLI                : %s\n"
		"Forced CLI type of number : %s\n"
		"CLI rewriting             : %s\n"
		"CLIR mode                 : %s\n"
		"CLIP enabled              : %s\n"
		"CLIP override             : %s\n"
		"CLIP default              : %s <%s>\n"
		"CLIP special arrangement  : %s\n",
		visdn_ton_to_string(
			ic->outbound_called_ton),
		ic->force_outbound_cli,
		visdn_ton_to_string(
			ic->force_outbound_cli_ton),
		ic->cli_rewriting ? "Yes" : "No",
		visdn_clir_mode_to_text(ic->clir_mode),
		ic->clip_enabled ? "Yes" : "No",
		ic->clip_override ? "Yes" : "No",
		ic->clip_default_name,
		ic->clip_default_number,
		ic->clip_special_arrangement ? "Yes" : "No");

	ast_cli(fd, "CLIP Numbers              : ");
	{
	struct visdn_number *num;
	list_for_each_entry(num, &ic->clip_numbers_list, node) {
		ast_cli(fd, "%s ", num->number);
	}
	}
	ast_cli(fd, "\n\n");

	if (intf->q931_intf) {
		if (intf->q931_intf->role == LAPD_INTF_ROLE_NT) {
			ast_cli(fd, "DLCs                      : ");

			struct q931_dlc *dlc;
			list_for_each_entry(dlc, &intf->q931_intf->dlcs,
					intf_node) {
				ast_cli(fd, "%d ", dlc->tei);

			}

			ast_cli(fd, "\n\n");

#define TIMER_CONFIG(timer)					\
	ast_cli(fd, #timer ": %3lld%-5s",			\
		intf->q931_intf->timer / 1000000LL,		\
		ic->timer ? " (*)" : "");

#define TIMER_CONFIG_LN(timer)					\
	ast_cli(fd, #timer ": %3lld%-5s\n",			\
		intf->q931_intf->timer / 1000000LL,		\
		ic->timer ? " (*)" : "");

			TIMER_CONFIG(T301);
			TIMER_CONFIG(T301);
			TIMER_CONFIG(T302);
			TIMER_CONFIG_LN(T303);
			TIMER_CONFIG(T304);
			TIMER_CONFIG(T305);
			TIMER_CONFIG(T306);
			ast_cli(fd, "T307: %d\n", ic->T307);
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
			ast_cli(fd, "T307: %d\n", ic->T307);
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

	ast_cli(fd, "\nParked calls:\n");
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

	ast_cli(fd, "\nChannels:\n");
	int i;
	for (i=0; i<intf->q931_intf->n_channels; i++) {
		ast_cli(fd, "  B%d: %s",
			intf->q931_intf->channels[i].id + 1,
			q931_channel_state_to_text(
				intf->q931_intf->channels[i].state));

		if (intf->q931_intf->channels[i].call) {
			struct q931_call *call =
				intf->q931_intf->channels[i].call;
			
			ast_cli(fd, "  Call: %5d.%c %s",
				call->call_reference,
				(call->direction ==
					Q931_CALL_DIRECTION_INBOUND)
						? 'I' : 'O',
				q931_call_state_to_text(call->state));
		}

		ast_cli(fd, "\n");
	}
}

char *visdn_intf_complete(char *line, char *word, int pos, int state)
{
	int which = 0;

	ast_mutex_lock(&visdn.lock);
	struct visdn_intf *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (!strncasecmp(word, intf->name, strlen(word))) {
			if (++which > state) {
				ast_mutex_unlock(&visdn.lock);
				return strdup(intf->name);
			}
		}
	}
	ast_mutex_unlock(&visdn.lock);

	return NULL;
}

static char *complete_show_visdn_interfaces(
		char *line, char *word, int pos, int state)
{
	if (pos != 3)
		return NULL;

	return visdn_intf_complete(line, word, pos, state);
}

static int do_show_visdn_interfaces(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&visdn.lock);

	if (argc == 3) {
		ast_cli(fd, "Interface  Role Mode TEI Status        Calls\n");
		
		struct visdn_intf *intf;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {

			ast_mutex_lock(&intf->lock);

			if (!intf->q931_intf) {
				ast_mutex_unlock(&intf->lock);
				continue;
			}

			char tei[6];

			if (intf->q931_intf->tei != LAPD_DYNAMIC_TEI)
				snprintf(tei, sizeof(tei), "%d",
					intf->q931_intf->tei);
			else
				strcpy(tei, "");

			int ncalls = 0;
			struct q931_call *call;
			list_for_each_entry(call, &intf->q931_intf->calls,
								calls_node)
				ncalls++;

			
			ast_cli(fd, "%-10s %-4s %-4s %-3s %-13s %d\n",
				intf->name,
				intf->q931_intf->role == LAPD_INTF_ROLE_NT ?
					"NT" : "TE",
				visdn_intf_mode_to_string_short(
					intf->q931_intf->mode),
				tei,
				visdn_intf_status_to_text(intf->status),
				ncalls);

			ast_mutex_unlock(&intf->lock);
		}
	} else if (argc == 4) {
		struct visdn_intf *intf;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {
			if (!strcasecmp(argv[3], intf->name)) {
				visdn_print_intf_details(fd, intf);
				break;
			}
		}
	} else
		return RESULT_SHOWUSAGE;

	ast_mutex_unlock(&visdn.lock);

	return RESULT_SUCCESS;
}

static char show_visdn_interfaces_help[] =
"Usage: visdn show interfaces [<interface>]\n"
"	Displays informations on vISDN's interfaces. If no interface name is\n"
"	specified, shows a summary of all the interfaces.\n";

static struct ast_cli_entry show_visdn_interfaces =
{
	{ "show", "visdn", "interfaces", NULL },
	do_show_visdn_interfaces,
	"Displays vISDN's interface information",
	show_visdn_interfaces_help,
	complete_show_visdn_interfaces,
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

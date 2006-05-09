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
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
//#include <asterisk/pbx.h>
#include <asterisk/options.h>
//#include <asterisk/utils.h>
//#include <asterisk/callerid.h>
#include <asterisk/cli.h>

#include "chan_visdn.h"
#include "util.h"
#include "huntgroup.h"

static struct visdn_huntgroup *visdn_hg_alloc(void)
{
	struct visdn_huntgroup *hg;

	hg = malloc(sizeof(*hg));
	if (!hg)
		return NULL;

	memset(hg, 0, sizeof(*hg));

	hg->refcnt = 1;

	INIT_LIST_HEAD(&hg->members);
	hg->configured = FALSE;

	return hg;
}

struct visdn_huntgroup *visdn_hg_get(struct visdn_huntgroup *hg)
{
	assert(hg);
	assert(hg->refcnt > 0);

	ast_mutex_lock(&visdn.usecnt_lock);
	hg->refcnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);

	return hg;
}

void visdn_hg_put(struct visdn_huntgroup *hg)
{
	assert(hg);
	assert(hg->refcnt > 0);

	ast_mutex_lock(&visdn.usecnt_lock);
	hg->refcnt--;
	ast_mutex_unlock(&visdn.usecnt_lock);

	if (!hg->refcnt)
		free(hg);
}

struct visdn_huntgroup *visdn_hg_get_by_name(const char *name)
{
	struct visdn_huntgroup *hg;

	ast_mutex_lock(&visdn.lock);
	
	list_for_each_entry(hg, &visdn.huntgroups_list, node) {
		if (!strcasecmp(hg->name, name)) {
			ast_mutex_unlock(&visdn.lock);
			return visdn_hg_get(hg);
		}
	}

	ast_mutex_unlock(&visdn.lock);

	return NULL;
}


static const char *visdn_huntgroup_mode_to_text(
	enum visdn_huntgroup_mode mode)
{
	switch(mode) {
	case VISDN_HUNTGROUP_MODE_SEQUENTIAL:
		return "sequential";
	case VISDN_HUNTGROUP_MODE_CYCLIC:
		return "cyclic";
	}

	return "*INVALID*";
}

static enum visdn_huntgroup_mode
	visdn_hg_string_to_mode(const char *str)
{
	if (!strcasecmp(str, "sequential"))
		return VISDN_HUNTGROUP_MODE_SEQUENTIAL;
	else if (!strcasecmp(str, "cyclic"))
		return VISDN_HUNTGROUP_MODE_CYCLIC;
	else {
		ast_log(LOG_ERROR,
			"Unknown huntgroup mode '%s'\n",
			str);

		return VISDN_HUNTGROUP_MODE_SEQUENTIAL;
	}
}

static void visdn_hg_clear_members(
	struct visdn_huntgroup *hg)
{
	struct visdn_huntgroup_member *hgm, *tpos;
	list_for_each_entry_safe(hgm, tpos, &hg->members, node) {
		visdn_intf_put(hgm->intf);
		hgm->intf = NULL;
		
		list_del(&hgm->node);
		free(hgm);
	}
}

static void visdn_hg_parse_members(
	struct visdn_huntgroup *hg,
	const char *value)
{
	char *str = strdup(value);
	char *strpos = str;
	char *tok;

	visdn_hg_clear_members(hg);

	while ((tok = strsep(&strpos, ","))) {
		while(*tok == ' ' || *tok == '\t')
			tok++;

		while(*(tok + strlen(tok) - 1) == ' ' ||
			*(tok + strlen(tok) - 1) == '\t')
			*(tok + strlen(tok) - 1) = '\0';

		struct visdn_intf *intf;
		intf = visdn_intf_get_by_name(tok);
		if (!intf) {
			ast_log(LOG_WARNING,
				"Huntgroup member %s not found\n",
				tok);

			continue;
		}
		
		struct visdn_huntgroup_member *hgm;
		hgm = malloc(sizeof(*hgm));
		memset(hgm, 0, sizeof(hgm));

		hgm->intf = visdn_intf_get(intf);

		list_add_tail(&hgm->node, &hg->members);

		visdn_intf_put(intf);
		intf = NULL;
	}

	free(str);
}

static int visdn_hg_from_var(
	struct visdn_huntgroup *hg,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "mode")) {
		hg->mode = visdn_hg_string_to_mode(var->value);
	} else if (!strcasecmp(var->name, "members")) {
		visdn_hg_parse_members(hg, var->value);
	} else {
		return -1;
	}

	return 0;
}

static void visdn_hg_reconfigure(
	struct ast_config *cfg,
	const char *cat,
	const char *name)
{
	struct visdn_huntgroup *hg;
	hg = visdn_hg_alloc();

	strncpy(hg->name, name, sizeof(hg->name));

	hg->configured = TRUE;

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)cat);
	while (var) {
		if (visdn_hg_from_var(hg, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	ast_mutex_lock(&visdn.lock);

	struct visdn_huntgroup *old_hg, *tpos;
	list_for_each_entry_safe(old_hg, tpos, &visdn.huntgroups_list, node) {
		if (strcasecmp(old_hg->name, name))
			continue;

		list_del(&old_hg->node);
		visdn_hg_put(old_hg);

		break;
	}

	list_add_tail(&visdn_hg_get(hg)->node, &visdn.huntgroups_list);

	ast_mutex_unlock(&visdn.lock);
}

void visdn_hg_reload(struct ast_config *cfg)
{
	ast_mutex_lock(&visdn.lock);

	/*
	struct visdn_huntgroup *hg;
	list_for_each_entry(hg, &visdn.huntgroups_list, node) {
		hg->configured = FALSE;
	}*/

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general") ||
		    !strcasecmp(cat, "global") ||
		    strncasecmp(cat, VISDN_HUNTGROUP_PREFIX,
				strlen(VISDN_HUNTGROUP_PREFIX)))
			continue;

		if (strlen(cat) <= strlen(VISDN_HUNTGROUP_PREFIX)) {
			ast_log(LOG_WARNING,
				"Empty huntgroup name in configuration\n");

			continue;
		}

		visdn_hg_reconfigure(cfg, cat,
			cat + strlen(VISDN_HUNTGROUP_PREFIX));
	}

	ast_mutex_unlock(&visdn.lock);
}

/*---------------------------------------------------------------------------*/

static char *complete_show_visdn_huntgroups(
		char *line, char *word, int pos, int state)
{
	if (pos != 3)
		return NULL;

	int which = 0;

	ast_mutex_lock(&visdn.lock);
	struct visdn_huntgroup *huntgroup;
	list_for_each_entry(huntgroup, &visdn.huntgroups_list, node) {
		if (!strncasecmp(word, huntgroup->name, strlen(word))) {
			if (++which > state) {
				ast_mutex_unlock(&visdn.lock);
				return strdup(huntgroup->name);
			}
		}
	}
	ast_mutex_unlock(&visdn.lock);

	return NULL;
}

static void do_show_visdn_huntgroups_details(
	int fd, struct visdn_huntgroup *hg)
{
	ast_cli(fd, "\n-- '%s'--\n", hg->name);

	ast_cli(fd, "Mode: %s\n",
		visdn_huntgroup_mode_to_text(hg->mode));

	ast_cli(fd, "Members: ");
	struct visdn_huntgroup_member *hgm;
	list_for_each_entry(hgm, &hg->members, node) {
		ast_cli(fd, "%s, ", hgm->intf->name);
	}
	ast_cli(fd, "\n");
}

static int do_show_visdn_huntgroups(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&visdn.lock);

	struct visdn_huntgroup *hg;
	list_for_each_entry(hg, &visdn.huntgroups_list, node) {
		if (argc != 4 || !strcasecmp(argv[3], hg->name))
			do_show_visdn_huntgroups_details(fd, hg);
	}

	ast_mutex_unlock(&visdn.lock);

	return RESULT_SUCCESS;
}

static char show_visdn_huntgroups_help[] =
"Usage: visdn show huntgroups [<huntgroup>]\n"
"	Displays detailed informations on vISDN's huntgroup or lists all the\n"
"	available huntgroups if <huntgroup> has not been specified.\n";

static struct ast_cli_entry show_visdn_huntgroups =
{
	{ "show", "visdn", "huntgroups", NULL },
	do_show_visdn_huntgroups,
	"Displays vISDN's huntgroups informations",
	show_visdn_huntgroups_help,
	complete_show_visdn_huntgroups
};

/*---------------------------------------------------------------------------*/

static struct visdn_huntgroup_member *visdn_hg_find_member(
	struct visdn_huntgroup *hg,
	const char *name)
{
	struct visdn_huntgroup_member *hgm;
	list_for_each_entry(hgm, &hg->members, node) {
		if (!strcasecmp(hgm->intf->name, name))
			return hgm;
	}

	return NULL;
}

static struct visdn_huntgroup_member *visdn_hg_next_member(
	struct visdn_huntgroup *hg,
	struct visdn_huntgroup_member *cur_memb)
{
	struct visdn_huntgroup_member *memb;

	ast_mutex_lock(&visdn.lock);

	if (cur_memb->node.next == &hg->members)
		memb = list_entry(hg->members.next,
			struct visdn_huntgroup_member, node);
	else
		memb = list_entry(cur_memb->node.next,
			struct visdn_huntgroup_member, node);

	ast_mutex_unlock(&visdn.lock);

	return memb;
}

struct visdn_intf *visdn_hg_hunt(
	struct visdn_huntgroup *hg,
	struct visdn_intf *cur_intf,
	struct visdn_intf *first_intf)
{
	ast_mutex_lock(&visdn.lock);

	if (list_empty(&hg->members)) {
		visdn_debug("No interfaces in huntgroup '%s'\n", hg->name);

		goto err_no_interfaces;
	}

	visdn_debug("Hunting started on group '%s'"
			" (mode='%s', cur_intf='%s', first_intf='%s',"
			" int_member='%s')\n",
			hg->name,
			visdn_huntgroup_mode_to_text(hg->mode),
			cur_intf ? cur_intf->name : "",
			first_intf ? first_intf->name : "",
			hg->current_member ?
				hg->current_member->intf->name : "");

	struct visdn_huntgroup_member *starting_hgm;
	if (cur_intf) {
		starting_hgm = visdn_hg_next_member(hg,
				visdn_hg_find_member(hg, cur_intf->name));
	} else {
		starting_hgm = list_entry(hg->members.next,
				struct visdn_huntgroup_member, node);

		switch(hg->mode) {
		case VISDN_HUNTGROUP_MODE_CYCLIC:
			if (hg->current_member) {
				starting_hgm = visdn_hg_next_member(
					hg, hg->current_member);
			}
		break;

		case VISDN_HUNTGROUP_MODE_SEQUENTIAL:
		break;
		}
	}

	struct visdn_huntgroup_member *hgm = starting_hgm;
	do {
		visdn_debug(
			"Huntgroup: trying interface '%s'\n",
			hgm->intf->name);

		if (hgm->intf == first_intf) {
			visdn_debug(
				"Huntgroup: cycle completed without success\n");
			break;
		}

		struct q931_interface *q931_intf = hgm->intf->q931_intf;

		if (q931_intf) {
			int i;
			for (i=0; i<q931_intf->n_channels; i++) {
				if (q931_intf->channels[i].state ==
						Q931_CHANSTATE_AVAILABLE) {

					hg->current_member = hgm;

					visdn_debug(
						"Huntgroup: found interface"
					       	" '%s'\n", hgm->intf->name);

					ast_mutex_unlock(&visdn.lock);

					return visdn_intf_get(hgm->intf);
				}
			}
		}

		hgm = visdn_hg_next_member(hg, hgm);

		visdn_debug(
			"Huntgroup: next interface '%s'\n",
			hgm->intf->name);

	} while(hgm != starting_hgm);

err_no_interfaces:
	ast_mutex_unlock(&visdn.lock);

	return NULL;
}

void visdn_hg_cli_register(void)
{
	ast_cli_register(&show_visdn_huntgroups);
}

void visdn_hg_cli_unregister(void)
{
	ast_cli_unregister(&show_visdn_huntgroups);
}

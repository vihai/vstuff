/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
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
#include <stdarg.h>
#include <ctype.h>

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/version.h>

#include "chan_vgsm.h"
#include "util.h"
#include "huntgroup.h"

static struct vgsm_huntgroup *vgsm_hg_alloc(void)
{
	struct vgsm_huntgroup *hg;

	hg = malloc(sizeof(*hg));
	if (!hg)
		return NULL;

	memset(hg, 0, sizeof(*hg));

	hg->refcnt = 1;

	INIT_LIST_HEAD(&hg->members);
	hg->configured = FALSE;

	return hg;
}

struct vgsm_huntgroup *vgsm_hg_get(struct vgsm_huntgroup *hg)
{
	assert(hg);
	assert(hg->refcnt > 0);
	assert(hg->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	hg->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return hg;
}

void vgsm_hg_put(struct vgsm_huntgroup *hg)
{
	assert(hg);
	assert(hg->refcnt > 0);
	assert(hg->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --hg->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt)
		free(hg);
}

struct vgsm_huntgroup *vgsm_hg_get_by_name(const char *name)
{
	struct vgsm_huntgroup *hg;

	ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);
	
	list_for_each_entry(hg, &vgsm.huntgroups_list, node) {
		if (!strcasecmp(hg->name, name)) {
			ast_rwlock_unlock(&vgsm.huntgroups_list_lock);
			return vgsm_hg_get(hg);
		}
	}

	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

	return NULL;
}


static const char *vgsm_huntgroup_mode_to_text(
	enum vgsm_huntgroup_mode mode)
{
	switch(mode) {
	case VGSM_HUNTGROUP_MODE_SEQUENTIAL:
		return "sequential";
	case VGSM_HUNTGROUP_MODE_CYCLIC:
		return "cyclic";
	}

	return "*INVALID*";
}

static enum vgsm_huntgroup_mode
	vgsm_hg_string_to_mode(const char *str)
{
	if (!strcasecmp(str, "sequential"))
		return VGSM_HUNTGROUP_MODE_SEQUENTIAL;
	else if (!strcasecmp(str, "cyclic"))
		return VGSM_HUNTGROUP_MODE_CYCLIC;
	else {
		ast_log(LOG_ERROR,
			"Unknown huntgroup mode '%s'\n",
			str);

		return VGSM_HUNTGROUP_MODE_SEQUENTIAL;
	}
}

static void vgsm_hg_clear_members(
	struct vgsm_huntgroup *hg)
{
	struct vgsm_huntgroup_member *hgm, *tpos;
	list_for_each_entry_safe(hgm, tpos, &hg->members, node) {
		vgsm_me_put(hgm->me);
		hgm->me = NULL;
		
		list_del(&hgm->node);
		free(hgm);
	}
}

static void vgsm_hg_parse_members(
	struct vgsm_huntgroup *hg,
	const char *value)
{
	char *str = strdup(value);
	char *strpos = str;
	char *tok;

	vgsm_hg_clear_members(hg);

	while ((tok = strsep(&strpos, ","))) {
		while(*tok == ' ' || *tok == '\t')
			tok++;

		while(*(tok + strlen(tok) - 1) == ' ' ||
			*(tok + strlen(tok) - 1) == '\t')
			*(tok + strlen(tok) - 1) = '\0';

		struct vgsm_me *me;
		me = vgsm_me_get_by_name(tok);
		if (!me) {
			ast_log(LOG_WARNING,
				"Huntgroup member %s not found\n",
				tok);

			continue;
		}
		
		struct vgsm_huntgroup_member *hgm;
		hgm = malloc(sizeof(*hgm));
		memset(hgm, 0, sizeof(hgm));

		hgm->me = vgsm_me_get(me);

		list_add_tail(&hgm->node, &hg->members);

		vgsm_me_put(me);
		me = NULL;
	}

	free(str);
}

static int vgsm_hg_from_var(
	struct vgsm_huntgroup *hg,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "mode")) {
		hg->mode = vgsm_hg_string_to_mode(var->value);
	} else if (!strcasecmp(var->name, "members")) {
		vgsm_hg_parse_members(hg, var->value);
	} else {
		return -1;
	}

	return 0;
}

static void vgsm_hg_reconfigure(
	struct ast_config *cfg,
	const char *cat,
	const char *name)
{
	struct vgsm_huntgroup *hg;
	hg = vgsm_hg_alloc();

	strncpy(hg->name, name, sizeof(hg->name));

	hg->configured = TRUE;

	struct ast_variable *var;
	var = ast_variable_browse(cfg, (char *)cat);
	while (var) {
		if (vgsm_hg_from_var(hg, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	ast_rwlock_wrlock(&vgsm.huntgroups_list_lock);

	struct vgsm_huntgroup *old_hg, *tpos;
	list_for_each_entry_safe(old_hg, tpos, &vgsm.huntgroups_list, node) {
		if (strcasecmp(old_hg->name, name))
			continue;

		list_del(&old_hg->node);
		vgsm_hg_put(old_hg);

		break;
	}

	list_add_tail(&vgsm_hg_get(hg)->node, &vgsm.huntgroups_list);

	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);
}

void vgsm_hg_reload(struct ast_config *cfg)
{
	/*
	 * TODO: Implement huntgroup removal
	 *
	struct vgsm_huntgroup *hg;
	list_for_each_entry(hg, &vgsm.huntgroups_list, node) {
		hg->configured = FALSE;
	}*/

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (strncasecmp(cat, VGSM_HUNTGROUP_PREFIX,
				strlen(VGSM_HUNTGROUP_PREFIX)))
			continue;

		if (strlen(cat) <= strlen(VGSM_HUNTGROUP_PREFIX)) {
			ast_log(LOG_WARNING,
				"Empty huntgroup name in configuration\n");

			continue;
		}

		vgsm_hg_reconfigure(cfg, cat,
			cat + strlen(VGSM_HUNTGROUP_PREFIX));
	}
}

/*---------------------------------------------------------------------------*/

static char *complete_show_vgsm_huntgroups(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	if (pos != 3)
		return NULL;

	int which = 0;

	ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);
	struct vgsm_huntgroup *huntgroup;
	list_for_each_entry(huntgroup, &vgsm.huntgroups_list, node) {
		if (!strncasecmp(word, huntgroup->name, strlen(word))) {
			if (++which > state) {
				char *name = strdup(huntgroup->name);
				ast_rwlock_unlock(&vgsm.huntgroups_list_lock);
				return name;
			}
		}
	}
	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

	return NULL;
}

static void do_show_vgsm_huntgroups_details(
	int fd, struct vgsm_huntgroup *hg)
{
	ast_cli(fd, "\n-- Huntgroup '%s'--\n", hg->name);

	ast_cli(fd, "Mode: %s\n",
		vgsm_huntgroup_mode_to_text(hg->mode));

	ast_cli(fd, "Members: ");
	struct vgsm_huntgroup_member *hgm;
	list_for_each_entry(hgm, &hg->members, node) {
		ast_cli(fd, "%s, ", hgm->me->name);
	}
	ast_cli(fd, "\n\n");
}

static int do_show_vgsm_huntgroups(int fd, int argc, char *argv[])
{
	ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);

	struct vgsm_huntgroup *hg;
	list_for_each_entry(hg, &vgsm.huntgroups_list, node) {
		if (argc != 4 || !strcasecmp(argv[3], hg->name))
			do_show_vgsm_huntgroups_details(fd, hg);
	}

	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

	return RESULT_SUCCESS;
}

static char show_vgsm_huntgroups_help[] =
"Usage: show vgsm huntgroups [<huntgroup>]\n"
"\n"
"	Displays detailed informations on vGSM's huntgroup or lists all the\n"
"	available huntgroups if <huntgroup> has not been specified.\n";

static struct ast_cli_entry show_vgsm_huntgroups =
{
	{ "show", "vgsm", "huntgroups", NULL },
	do_show_vgsm_huntgroups,
	"Displays vGSM's huntgroups informations",
	show_vgsm_huntgroups_help,
	complete_show_vgsm_huntgroups
};

/*---------------------------------------------------------------------------*/

static struct vgsm_huntgroup_member *vgsm_hg_find_member(
	struct vgsm_huntgroup *hg,
	const char *name)
{
	struct vgsm_huntgroup_member *hgm;
	list_for_each_entry(hgm, &hg->members, node) {
		if (!strcasecmp(hgm->me->name, name))
			return hgm;
	}

	return NULL;
}

static struct vgsm_huntgroup_member *vgsm_hg_next_member(
	struct vgsm_huntgroup *hg,
	struct vgsm_huntgroup_member *cur_memb)
{
	struct vgsm_huntgroup_member *memb;

	ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);

	if (cur_memb->node.next == &hg->members)
		memb = list_entry(hg->members.next,
			struct vgsm_huntgroup_member, node);
	else
		memb = list_entry(cur_memb->node.next,
			struct vgsm_huntgroup_member, node);

	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

	return memb;
}

struct vgsm_me *vgsm_hg_hunt(
	struct vgsm_huntgroup *hg,
	struct vgsm_me *cur_me,
	struct vgsm_me *first_me)
{
	ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);

	if (list_empty(&hg->members)) {
		vgsm_debug_generic("No interfaces in huntgroup '%s'\n",
				hg->name);

		goto err_no_interfaces;
	}

	vgsm_debug_generic("Hunting started on group '%s'"
			" (mode='%s', cur_me='%s', first_me='%s',"
			" int_member='%s')\n",
			hg->name,
			vgsm_huntgroup_mode_to_text(hg->mode),
			cur_me ? cur_me->name : "",
			first_me ? first_me->name : "",
			hg->current_member ?
				hg->current_member->me->name : "");

	struct vgsm_huntgroup_member *starting_hgm;
	if (cur_me) {
		starting_hgm = vgsm_hg_next_member(hg,
				vgsm_hg_find_member(hg, cur_me->name));
	} else {
		starting_hgm = list_entry(hg->members.next,
				struct vgsm_huntgroup_member, node);

		switch(hg->mode) {
		case VGSM_HUNTGROUP_MODE_CYCLIC:
			if (hg->current_member) {
				starting_hgm = vgsm_hg_next_member(
					hg, hg->current_member);
			}
		break;

		case VGSM_HUNTGROUP_MODE_SEQUENTIAL:
		break;
		}
	}

	struct vgsm_huntgroup_member *hgm = starting_hgm;
	do {
		vgsm_debug_generic(
			"Huntgroup: trying interface '%s'\n",
			hgm->me->name);

		if (hgm->me == first_me) {
			vgsm_debug_generic(
				"Huntgroup: cycle completed without success\n");
			break;
		}

		ast_mutex_lock(&hgm->me->lock);

		if (hgm->me->status == VGSM_ME_STATUS_READY &&
		   !hgm->me->vgsm_chan &&
		   (hgm->me->net.status ==
					VGSM_NET_STATUS_REGISTERED_HOME ||
		    hgm->me->net.status ==
					VGSM_NET_STATUS_REGISTERED_ROAMING)) {

			hg->current_member = hgm;

			vgsm_debug_generic(
				"Huntgroup: found me"
				" '%s'\n", hgm->me->name);

			ast_mutex_unlock(&hgm->me->lock);
			ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

			return vgsm_me_get(hgm->me);
		}

		ast_mutex_unlock(&hgm->me->lock);

		hgm = vgsm_hg_next_member(hg, hgm);

		vgsm_debug_generic(
			"Huntgroup: next interface '%s'\n",
			hgm->me->name);

	} while(hgm != starting_hgm);

err_no_interfaces:
	ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

	return NULL;
}

int vgsm_hg_me_load(void)
{
	ast_cli_register(&show_vgsm_huntgroups);

	return 0;
}

int vgsm_hg_me_unload(void)
{
	ast_cli_unregister(&show_vgsm_huntgroups);

	return 0;
}

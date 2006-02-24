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

#ifndef _HUNTGROUP_H
#define _HUNTGROUP_H

#include <list.h>

#define VISDN_HUNTGROUP_PREFIX "huntgroup:"

enum visdn_huntgroup_mode
{
	VISDN_HUNTGROUP_MODE_SEQUENTIAL,
	VISDN_HUNTGROUP_MODE_CYCLIC,
};

struct visdn_intf;

struct visdn_huntgroup_member
{
	struct list_head node;

	struct visdn_intf *intf;
};

struct visdn_huntgroup
{
	struct list_head node;

	int refcnt;

	int configured;

	char name[32];
	enum visdn_huntgroup_mode mode;
	struct list_head members;

	struct visdn_huntgroup_member *current_member;
};

void visdn_hg_reload(struct ast_config *cfg);

struct visdn_huntgroup *visdn_hg_get(struct visdn_huntgroup *hg);
void visdn_hg_put(struct visdn_huntgroup *hg);
struct visdn_huntgroup *visdn_hg_get_by_name(const char *name);

struct visdn_intf *visdn_hg_hunt(
	struct visdn_huntgroup *hg,
	struct visdn_intf *cur_intf,
	struct visdn_intf *first_intf);

void visdn_hg_cli_register(void);
void visdn_hg_cli_unregister(void);

#endif

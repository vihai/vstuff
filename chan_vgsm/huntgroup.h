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

#ifndef _VGSM_HUNTGROUP_H
#define _VGSM_HUNTGROUP_H

#include <list.h>

#define VGSM_HUNTGROUP_PREFIX "huntgroup:"

enum vgsm_huntgroup_mode
{
	VGSM_HUNTGROUP_MODE_SEQUENTIAL,
	VGSM_HUNTGROUP_MODE_CYCLIC,
};

struct vgsm_module;

struct vgsm_huntgroup_member
{
	struct list_head node;

	struct vgsm_module *module;
};

struct vgsm_huntgroup
{
	struct list_head node;

	int refcnt;

	int configured;

	char name[32];
	enum vgsm_huntgroup_mode mode;
	struct list_head members;

	struct vgsm_huntgroup_member *current_member;
};

void vgsm_hg_reload(struct ast_config *cfg);

struct vgsm_huntgroup *vgsm_hg_get(struct vgsm_huntgroup *hg);
void vgsm_hg_put(struct vgsm_huntgroup *hg);
struct vgsm_huntgroup *vgsm_hg_get_by_name(const char *name);

struct vgsm_module *vgsm_hg_hunt(
	struct vgsm_huntgroup *hg,
	struct vgsm_module *cur_module,
	struct vgsm_module *first_module);

int vgsm_hg_module_load(void);
int vgsm_hg_module_unload(void);

#endif

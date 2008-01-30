/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <locale.h>
#include <iconv.h>

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>

#include "chan_vgsm.h"
#include "util.h"
#include "sim.h"

struct vgsm_sim *vgsm_sim_alloc(void)
{
	struct vgsm_sim *sim;

	sim = malloc(sizeof(*sim));
	if (!sim)
		return NULL;

	memset(sim, 0, sizeof(*sim));

	sim->refcnt = 1;

	return sim;
}

struct vgsm_sim *vgsm_sim_get(
	struct vgsm_sim *sim)
{
	assert(sim);
	assert(sim->refcnt > 0);
	assert(sim->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sim->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sim;
}

void _vgsm_sim_put(struct vgsm_sim *sim)
{
	assert(sim);
	assert(sim->refcnt > 0);
	assert(sim->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --sim->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt)
		free(sim);
}

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

#ifndef _VGSM_SIM_H
#define _VGSM_SIM_H

struct vgsm_sim
{
	struct list_head node;

	int refcnt;

	char pin[16];
};

struct vgsm_sim *vgsm_sim_alloc(void);
struct vgsm_sim *vgsm_sim_get(struct vgsm_sim *sim);
void _vgsm_sim_put(struct vgsm_sim *sim);
#define vgsm_sim_put(sim) \
	do { _vgsm_sim_put(sim); (sim) = NULL; } while(0)

//void vgsm_sim_cli_register(void);
//void vgsm_sim_cli_unregister(void);

#endif

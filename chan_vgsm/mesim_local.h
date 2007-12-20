/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_MESIM_LOCAL_H
#define _VGSM_MESIM_LOCAL_H

#include <list.h>

#include "timer.h"
#include "util.h"

struct vgsm_mesim;

struct vgsm_mesim_local
{
	struct vgsm_mesim_driver driver;

	struct vgsm_mesim *mesim;

	char device_filename[PATH_MAX];
	int fd;
	int sim_id;
};

struct vgsm_mesim_driver *vgsm_mesim_local_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset);
void vgsm_mesim_local_destroy(struct vgsm_mesim_driver *driver);

#endif

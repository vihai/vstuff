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

#ifndef _VGSM_MESIM_CLNT_H
#define _VGSM_MESIM_CLNT_H

#include "timer.h"
#include "util.h"

struct vgsm_mesim;

struct vgsm_mesim_clnt
{
	struct vgsm_mesim_driver driver;

	struct vgsm_mesim *mesim;

	int listen_fd;
	int sock_fd;
	struct sockaddr_in bind_addr;
};

struct vgsm_mesim_driver *vgsm_mesim_clnt_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset);
void vgsm_mesim_clnt_destroy(struct vgsm_mesim_driver *driver);

#endif

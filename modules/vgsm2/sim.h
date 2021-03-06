/*
 * VoiSmart vGSM-II board driver
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

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>

#include "uart.h"

enum vgsm_sim_status
{
	VGSM_SIM_STATUS_IDENTIFY,
};

struct vgsm_sim
{
	struct kobject kobj;

	int id;

	struct vgsm_card *card;

	struct vgsm_uart uart;

	unsigned long status;
	int clock;
};

struct vgsm_sim *vgsm_sim_create(
	struct vgsm_sim *sim,
	struct vgsm_card *card,
	int id,
	u32 uart_base);
void vgsm_sim_destroy(struct vgsm_sim *sim);

int vgsm_sim_register(struct vgsm_sim *sim);
void vgsm_sim_unregister(struct vgsm_sim *sim);

void vgsm_sim_update_sim_setup(struct vgsm_sim *sim);

int __init vgsm_sim_modinit(void);
void __exit vgsm_sim_modexit(void);

#endif

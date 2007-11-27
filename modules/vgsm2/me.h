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

#ifndef _VGSM_ME_H
#define _VGSM_ME_H

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/feature.h>

#include <linux/kstreamer/amu_compander.h>

#include "uart.h"

enum vgsm_me_status
{
	VGSM_ME_STATUS_RUNNING,
	VGSM_ME_STATUS_ON,
	VGSM_ME_STATUS_IDENTIFY,
};

struct vgsm_card;

struct vgsm_amu_compander
{
	struct ks_feature_value feature;
	struct ks_amu_compander_descr descr;
};

struct vgsm_me_rx
{
	struct ks_chan ks_chan;

	struct vgsm_amu_compander amu_compander;

	struct vgsm_me *me;

	BOOL compander_enabled;
	BOOL compander_mu_mode;

	u16 fifo_size;
	u32 fifo_base;
	u32 fifo_out;
};

struct vgsm_amu_decompander
{
	struct ks_feature_value feature;
	struct ks_amu_compander_descr descr;
};

struct vgsm_me_tx
{
	struct ks_chan ks_chan;

	struct vgsm_amu_decompander amu_decompander;

	struct vgsm_me *me;

	BOOL compander_enabled;
	BOOL compander_mu_mode;

	u16 fifo_size;
	u32 fifo_base;
	u32 fifo_in;
};

struct vgsm_me
{
	struct ks_node ks_node;

	struct vgsm_me_rx rx;
	struct vgsm_me_tx tx;

	struct vgsm_card *card;

	int id;

	int route_to_sim;

	struct vgsm_uart asc0;
	struct vgsm_uart asc1;
	struct vgsm_uart mesim;

	unsigned long status;
};

struct vgsm_me *vgsm_me_create(
	struct vgsm_me *me,
	struct vgsm_card *card,
	int id,
	const char *name,
	u32 rx_fifo_base,
	u32 rx_fifo_size,
	u32 tx_fifo_base,
	u32 tx_fifo_size,
	u32 asc0_base,
	u32 asc1_base,
	u32 mesim_base);
void vgsm_me_destroy(struct vgsm_me *me);

struct vgsm_me *vgsm_me_get(struct vgsm_me *me);
void vgsm_me_put(struct vgsm_me *me);

int vgsm_me_register(struct vgsm_me *me);
void vgsm_me_unregister(struct vgsm_me *me);

BOOL vgsm_me_power_get(struct vgsm_me *me);

int __init vgsm_me_modinit(void);
void __exit vgsm_me_modexit(void);

#endif

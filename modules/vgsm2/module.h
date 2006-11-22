/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_MODULE_H
#define _VGSM_MODULE_H

#include <linux/serial_8250.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>

enum vgsm_module_status
{
	VGSM_MODULE_STATUS_RUNNING,
	VGSM_MODULE_STATUS_ON,
};

struct vgsm_card;

struct vgsm_module_rx
{
	struct ks_chan ks_chan;

	struct vgsm_module *module;

	u16 fifo_size;
	u16 fifo_base;
	u16 fifo_out;
};

struct vgsm_module_tx
{
	struct ks_chan ks_chan;

	struct vgsm_module *module;

	u16 fifo_size;
	u16 fifo_base;
	u16 fifo_in;
};

struct vgsm_module
{
	struct ks_node ks_node;

	struct vgsm_module_rx rx;
	struct vgsm_module_tx tx;

	struct vgsm_card *card;

	int id;

	struct uart_port asc0;
	int asc0_line;

	struct uart_port asc1;
	int asc1_line;

	struct uart_port sim;
	int sim_line;

	struct uart_port mesim;
	int mesim_line;

	unsigned long status;
};

struct vgsm_module *vgsm_module_get(struct vgsm_module *module);
void vgsm_module_put(struct vgsm_module *module);
void vgsm_module_init(
	struct vgsm_module *module,
	struct vgsm_card *card,
	int id,
	const char *name,
	u16 rx_fifo_base,
	u16 rx_fifo_size,
	u16 tx_fifo_base,
	u16 tx_fifo_size,
	u16 asc0_base,
	u16 asc1_base,
	u16 sim_base,
	u16 mesim_base);
struct vgsm_module *vgsm_module_alloc(
	struct vgsm_card *card,
	int id,
	const char *name,
	u16 rx_fifo_base,
	u16 rx_fifo_size,
	u16 tx_fifo_base,
	u16 tx_fifo_size,
	u16 asc0_base,
	u16 asc1_base,
	u16 sim_base,
	u16 mesim_base);
int vgsm_module_register(struct vgsm_module *module);
void vgsm_module_unregister(struct vgsm_module *module);

#endif

/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_MODULE_H
#define _VGSM_MODULE_H

#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <linux/visdn/port.h>
#include <linux/visdn/chan.h>

enum vgsm_module_status
{
	VGSM_MODULE_STATUS_RX_ACK_PENDING,
	VGSM_MODULE_STATUS_TX_ACK_PENDING,
};

struct vgsm_card;
struct vgsm_module
{
	struct vgsm_card *card;

	int id;

	dev_t devt;
	BOOL is_open;

	struct class_device class_device;
	
	/* kfifo management */
	struct kfifo *kfifo_rx;
	struct kfifo *kfifo_tx;
	spinlock_t kfifo_rx_lock;
	spinlock_t kfifo_tx_lock;

	/* Wait queue */
	wait_queue_head_t tx_wait_queue;
	wait_queue_head_t rx_wait_queue;

	/* One port per card */
	struct visdn_port visdn_port;

	/* One channel per port */
	struct visdn_chan visdn_chan;

	unsigned long status;

	u8 rx_gain;
	u8 tx_gain;

	BOOL rx_pre;
	BOOL tx_pre;

	BOOL anal_loop;
	BOOL dig_loop;
};

void vgsm_module_send_string(
	struct vgsm_module *module,
	u8 *buf,
	int len);
void vgsm_module_send_ack(
	struct vgsm_module *module);
void vgsm_module_send_onoff(
	struct vgsm_module *module,
	int onoff_cmd);

void vgsm_module_init(
	struct vgsm_module *module,
	struct vgsm_card *card,
	int id);
int vgsm_module_register(
	struct vgsm_module *module,
	struct vgsm_card *card);
void vgsm_module_unregister(
	struct vgsm_module *module);

#endif

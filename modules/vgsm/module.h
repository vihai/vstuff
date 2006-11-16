/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 * Copyright (C) 2006 Daniele Orlandi
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

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>

enum vgsm_module_status
{
	VGSM_MODULE_STATUS_RUNNING,
	VGSM_MODULE_STATUS_RX_THROTTLE,
	VGSM_MODULE_STATUS_RX_ACK_PENDING,
	VGSM_MODULE_STATUS_TX_ACK_PENDING,
	VGSM_MODULE_STATUS_ON,
};

struct vgsm_card;

struct vgsm_module_rx
{
	struct ks_chan ks_chan;

	struct vgsm_module *module;

	int fifo_pos;
	int fifo_size;

	u8 codec_gain;
};

struct vgsm_module_tx
{
	struct ks_chan ks_chan;

	struct vgsm_module *module;

	int fifo_pos;
	int fifo_size;

	struct kfifo *fifo;
	spinlock_t fifo_lock;

	wait_queue_head_t wait_queue;

	u8 codec_gain;
};

struct vgsm_module
{
	struct ks_node ks_node;

	struct vgsm_module_rx rx;
	struct vgsm_module_tx tx;

	struct vgsm_card *card;
	struct vgsm_micro *micro;

	int id;
	int timeslot_offset;

	struct tty_struct *tty;
	atomic_t tty_open_count;

	struct completion read_status_completion;

	struct timer_list ack_timeout_timer;

	unsigned long status;

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
void vgsm_module_send_set_padding_timeout(
	struct vgsm_module *module,
	u8 timeout);
void vgsm_module_send_power_get(
	struct vgsm_module *module);

struct vgsm_module *vgsm_module_get(struct vgsm_module *module);
void vgsm_module_put(struct vgsm_module *module);
void vgsm_module_init(
	struct vgsm_module *module,
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name);
struct vgsm_module *vgsm_module_alloc(
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name);
int vgsm_module_register(struct vgsm_module *module);
void vgsm_module_unregister(struct vgsm_module *module);

#endif

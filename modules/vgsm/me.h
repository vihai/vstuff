/*
 * VoiSmart vGSM-I card driver
 *
 * Copyright (C) 2005-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_ME_H
#define _VGSM_ME_H

#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>

enum vgsm_me_status
{
	VGSM_ME_STATUS_RUNNING,
	VGSM_ME_STATUS_RX_THROTTLE,
	VGSM_ME_STATUS_RX_ACK_PENDING,
	VGSM_ME_STATUS_TX_ACK_PENDING,
	VGSM_ME_STATUS_ON,
};

struct vgsm_card;

struct vgsm_me_rx
{
	struct ks_chan ks_chan;

	struct vgsm_me *me;

	int fifo_pos;
	int fifo_size;

	u8 codec_gain;
};

struct vgsm_me_tx
{
	struct ks_chan ks_chan;

	struct vgsm_me *me;

	int fifo_pos;
	int fifo_size;
	BOOL fifo_underrun;

	struct kfifo *fifo;
	spinlock_t fifo_lock;

	wait_queue_head_t wait_queue;

	u8 codec_gain;
};

struct vgsm_me
{
	struct ks_node ks_node;

	struct vgsm_me_rx rx;
	struct vgsm_me_tx tx;

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

void vgsm_me_send_string(
	struct vgsm_me *me,
	u8 *buf,
	int len);
void vgsm_me_send_ack(
	struct vgsm_me *me);
void vgsm_me_send_onoff(
	struct vgsm_me *me,
	int onoff_cmd);
void vgsm_me_send_set_padding_timeout(
	struct vgsm_me *me,
	u8 timeout);
void vgsm_me_send_power_get(
	struct vgsm_me *me);

struct vgsm_me *vgsm_me_create(
	struct vgsm_me *me,
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name);

struct vgsm_me *vgsm_me_get(struct vgsm_me *me);
void vgsm_me_put(struct vgsm_me *me);

int vgsm_me_register(struct vgsm_me *me);
void vgsm_me_unregister(struct vgsm_me *me);

#endif

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

enum vgsm_mesim_local_type
{
	VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT,
	VGSM_MESIM_LOCAL_TYPE_VGSM,
	VGSM_MESIM_LOCAL_TYPE_USB,
};

enum vgsm_mesim_local_state
{
	VGSM_MESIM_LOCAL_STATE_NULL,
	VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED,
	VGSM_MESIM_LOCAL_STATE_RESET,
	VGSM_MESIM_LOCAL_STATE_READING_ATR,
	VGSM_MESIM_LOCAL_STATE_READY,
	VGSM_MESIM_LOCAL_STATE_DIRECTLY_ROUTED,
	VGSM_MESIM_LOCAL_STATE_FAILED,
};

struct vgsm_mesim;

struct vgsm_mesim_local
{
	struct vgsm_mesim_driver driver;

	struct vgsm_mesim *mesim;

	char device_filename[PATH_MAX];
	int fd;
	int sim_id;
	int card_id;

	enum vgsm_mesim_local_type type;

	enum vgsm_mesim_local_state state;

	__u8 out_buf[64];
	int out_buf_len;
	__u8 atr_buf[64];
	int atr_buf_len;

	pthread_t modem_thread;
	BOOL modem_thread_has_to_exit;

	struct vgsm_timer timer;
};

struct vgsm_mesim_driver *vgsm_mesim_local_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset);
void vgsm_mesim_local_destroy(struct vgsm_mesim_driver *driver);

#endif

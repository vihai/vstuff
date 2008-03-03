/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2007-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_MESIM_IMPL_H
#define _VGSM_MESIM_IMPL_H

#include <linux/serial.h>

#include <asterisk/lock.h>

#include <list.h>

#include "timer.h"
#include "util.h"

enum vgsm_mesim_impl_state
{
	VGSM_MESIM_IMPL_STATE_NULL,
	VGSM_MESIM_IMPL_STATE_TRYING,
	VGSM_MESIM_IMPL_STATE_READY,
};

enum vgsm_mesim_impl_parser_state
{
	VGSM_MESIM_IMPL_PARSER_STATE_IDLE,
	VGSM_MESIM_IMPL_PARSER_STATE_READING_HEX_OCTET,
};

struct vgsm_mesim;

struct vgsm_mesim_impl
{
	struct vgsm_mesim_driver driver;

	struct vgsm_mesim *mesim;

	int sock_fd;
	struct sockaddr_in client_addr;

	ast_mutex_t state_lock;
	enum vgsm_mesim_impl_state state;
	enum vgsm_mesim_impl_parser_state parser_state;
	__u8 hexval;

	struct vgsm_timer timer;
};

struct vgsm_mesim_driver *vgsm_mesim_impl_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset);
void vgsm_mesim_impl_destroy(struct vgsm_mesim_driver *driver);

#endif

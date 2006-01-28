/*
 * vISDN echo canceller module
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VEC_EC_H
#define _VEC_EC_H

/* See core.h for IOC allocation */
#define VEC_GET_FAREND_CHANID	_IOR(0xd0, 0x50, unsigned int)
#define VEC_GET_NEAREND_CHANID	_IOR(0xd0, 0x51, unsigned int)
#define VEC_START		_IOR(0xd0, 0x52, unsigned int)
#define VEC_STOP		_IOR(0xd0, 0x53, unsigned int)

#ifdef __KERNEL__

#include <linux/kfifo.h>

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>
#include <linux/visdn/leg.h>
#include <linux/visdn/port.h>

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vec_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vec_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vec_debug(format, arg...) do {} while (0)
#endif

#define vec_msg(level, format, arg...)				\
	printk(level vec_MODULE_PREFIX				\
		format,						\
		## arg)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define vec_MODULE_NAME "visdn-ec-kb"
#define vec_MODULE_PREFIX vec_MODULE_NAME ": "
#define vec_MODULE_DESCR " echo canceller module for vISDN"

#define to_vec_ec(vport) \
		container_of((vport), struct vec_ec, visdn_port)
#define ne_to_vec_ec(vchan) \
		container_of((vchan), struct vec_ec, visdn_chan_ne)
#define fe_to_vec_ec(vchan) \
		container_of((vchan), struct vec_ec, visdn_chan_fe)

enum vec_state
{
	VEC_OFF,
	VEC_PRE_TRAINING,
	VEC_TRAINING,
	VEC_ACTIVE,
};

enum vec_flags
{
	VEC_FE_NE_CYCLE,
};

struct vec_ec
{
	struct visdn_port visdn_port;
	struct visdn_chan visdn_chan_ne;
	struct visdn_chan visdn_chan_fe;

	struct list_head node;
	int id;

	enum vec_state ec_state;
	unsigned long flags;

	echo_can_state_t *ec;

	struct kfifo *fe_w_fifo;
	spinlock_t fe_w_fifo_lock;

	struct kfifo *fe_r_fifo;
	spinlock_t fe_r_fifo_lock;

	struct kfifo *ne_r_fifo;
	spinlock_t ne_r_fifo_lock;

	int training_pos;
	int pre_training_timer;
};

#endif

#endif

/*
 * vISDN gateway between vISDN's crossconnector and userland for stream access
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_STREAMPORT_H
#define _VISDN_STREAMPORT_H

#ifdef __KERNEL__

#include <linux/kfifo.h>

#include <linux/visdn/core.h>

#define vsp_MODULE_NAME "visdn-streamport"
#define vsp_MODULE_PREFIX vsp_MODULE_NAME ": "
#define vsp_MODULE_DESCR "vISDN streamport module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vsp_chan(vchan) container_of((vchan), struct vsp_chan, visdn_chan)

struct vsp_chan
{
	struct visdn_chan visdn_chan;

	struct kfifo *rx_fifo;
	spinlock_t rx_fifo_lock;
	struct kfifo *tx_fifo;
	spinlock_t tx_fifo_lock;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vsp_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vsp_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vsp_debug(format, arg...) do {} while (0)
#endif

#define vsp_msg(level, format, arg...)				\
	printk(level vsp_MODULE_PREFIX				\
		format,						\
		## arg)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif

#endif

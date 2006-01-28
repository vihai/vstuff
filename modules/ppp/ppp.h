/*
 * vISDN gateway between vISDN's crossconnector and Linux's ppp subsystem
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_PPP_H
#define _VISDN_PPP_H

/* See core.h for IOC allocation */
#define VISDN_PPP_GET_CHANID	_IOR(0xd0, 0x30, unsigned int)

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/ppp_channel.h>

#define vppp_MODULE_NAME "visdn-ppp"
#define vppp_MODULE_PREFIX vppp_MODULE_NAME ": "
#define vppp_MODULE_DESCR "vISDN ppp gateway module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vppp_chan(vchan) container_of(vchan, struct vppp_chan, visdn_chan)

enum vppp_chan_status
{
	VPPP_CHAN_STATUS_QUEUE_STOPPED,
};

struct vppp_chan
{
	struct visdn_chan visdn_chan;

	struct ppp_channel ppp_chan;

	unsigned long status;

	struct tasklet_struct wakeup_tasklet;
	struct tasklet_struct rx_tasklet;
	struct tasklet_struct rx_error_tasklet;

	struct sk_buff_head rx_queue;
	spinlock_t rx_queue_lock;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vppp_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vppp_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vppp_debug(format, arg...) do {} while (0)
#endif

#define vppp_msg(level, format, arg...)				\
	printk(level vppp_MODULE_PREFIX				\
		format,						\
		## arg)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

extern int debug_level;

#endif

#endif

/*
 * vISDN gateway between vISDN's softswitch and userland for stream access
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_MILLIWATT_H
#define _VISDN_MILLIWATT_H

#ifdef __KERNEL__

#include <linux/kfifo.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>

#define vmw_MODULE_NAME "visdn-milliwatt"
#define vmw_MODULE_PREFIX vmw_MODULE_NAME ": "
#define vmw_MODULE_DESCR "vISDN milliwatt module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vmw_chan(vchan) container_of((vchan), struct vmw_chan, visdn_chan)

struct vmw_chan
{
	struct list_head node;

	struct ks_node ks_node;
	struct ks_chan ks_chan;

	int id;

	int pos;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vmw_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vmw_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vmw_debug(format, arg...) do {} while (0)
#endif

#define vmw_msg(level, format, arg...)				\
	printk(level vmw_MODULE_PREFIX				\
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

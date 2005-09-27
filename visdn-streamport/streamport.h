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

#include <visdn.h>

#define vsp_MODULE_NAME "visdn-streamport"
#define vsp_MODULE_PREFIX vsp_MODULE_NAME ": "
#define vsp_MODULE_DESCR "vISDN streamport module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vsp_chan(visdn_chan) container_of(visdn_chan, struct vsp_chan, visdn_chan)

struct vsp_chan
{
	int index;

	struct hlist_node index_hlist_node;

	struct visdn_chan visdn_chan;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vsp_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vsp_MODULE_PREFIX		\
			": "					\
			format,					\
			## arg)
#else
#define vsp_debug(format, arg...) do {} while (0)
#endif

#define vsp_msg(level, format, arg...)				\
	printk(level vsp_MODULE_PREFIX				\
		": "						\
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

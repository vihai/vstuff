/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_CORE_H
#define _VISDN_CORE_H

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/netdevice.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

extern dev_t visdn_first_dev;
extern struct device visdn_system_device;
extern struct class visdn_system_class;

static inline struct sk_buff *visdn_alloc_skb(unsigned int length)
{
	return dev_alloc_skb(length);
}

static inline void visdn_kfree_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define visdn_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG visdn_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define visdn_debug(format, arg...) do {} while (0)
#endif

#define visdn_msg(level, format, arg...)			\
	printk(level visdn_MODULE_PREFIX			\
		format,						\
		## arg)

#endif

extern int debug_level;

#endif

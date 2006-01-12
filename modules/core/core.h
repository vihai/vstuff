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
#include <linux/ioctl.h>

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

/*
 * IOC allocation:
 * 0  - ?
 * 1  - ?
 * 2  - IOC_CONNECT
 * 3  - IOC_DISCONNECT
 * 4  - IOC_CONNECT_PATH
 * 5  - IOC_DISCONNECT_PATH
 * 6  - IOC_ENABLE_PATH
 * 7  - IOC_DISABLE_PATH
 * 8  - EC_GET_FAREND_CHANID
 * 9  - EC_GET_NEAREND_CHANID
 * 10 - SP_GET_CHANID
 * 11 - PPP_GET_CHANID
 * 12 - NETDEV_GET_CHANID
 * 13 - NETDEV_GET_E_CHANID
 *
 */

enum visdn_notify
{
	VISDN_NOTIFY_PORT_REGISTERED,
	VISDN_NOTIFY_PORT_UNREGISTERED,
	VISDN_NOTIFY_PORT_ENABLED,
	VISDN_NOTIFY_PORT_DISABLED,
	VISDN_NOTIFY_PORT_CONNECTED,
	VISDN_NOTIFY_PORT_DISCONNECTED,
	VISDN_NOTIFY_PORT_ACTIVATED,
	VISDN_NOTIFY_PORT_DEACTIVATED,
	VISDN_NOTIFY_PORT_ERROR_INDICATION,
	VISDN_NOTIFY_CHAN_REGISTERED,
	VISDN_NOTIFY_CHAN_UNREGISTERED,
	VISDN_NOTIFY_CHAN_ENABLED,
	VISDN_NOTIFY_CHAN_DISABLED,
};

extern int visdn_register_notifier(struct notifier_block *nb);
extern int visdn_unregister_notifier(struct notifier_block *nb);
extern int visdn_call_notifiers(unsigned long val, void *v);

extern int debug_level;

#endif

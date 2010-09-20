/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_H
#define _VISDN_H

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/ioctl.h>

extern dev_t visdn_first_dev;
extern struct device visdn_system_device;
extern struct class visdn_system_class;

extern struct rw_semaphore visdn_kset_rwsem;
extern struct kset *visdn_kset;

extern struct sk_buff *visdn_alloc_skb(unsigned int length);

static inline void visdn_kfree_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

/*
 * IOC allocation:
 * 0x00 - DEV_IOC_ACTIVATE
 * 0x01 - DEV_IOC_DEACTIVATE
 * 0x02 - IOC_CONNECT
 * 0x03 - IOC_DISCONNECT
 * 0x05 - IOC_DISCONNECT_ENDPOINT
 * 0x06 - IOC_PIPELINE_OPEN
 * 0x07 - IOC_PIPELINE_CLOSE
 * 0x08 - IOC_PIPELINE_START
 * 0x09 - IOC_PIPELINE_STOP
 * 0x20 - SP_GET_CHANID
 * 0x21 - SP_GET_PRESSURE
 * 0x30 - PPP_GET_CHANID
 * 0x40 - NETDEV_GET_CHANID
 * 0x41 - NETDEV_GET_E_CHANID
 * 0x50 - EC_GET_FAREND_CHANID
 * 0x51 - EC_GET_NEAREND_CHANID
 * 0x52 - EC_START
 * 0x53 - EC_STOP
 *
 */

enum visdn_event
{
	VISDN_EVENT_PORT_REGISTERED,
	VISDN_EVENT_PORT_UNREGISTERED,
	VISDN_EVENT_PORT_ENABLED,
	VISDN_EVENT_PORT_DISABLED,
	VISDN_EVENT_PORT_CONNECTED,
	VISDN_EVENT_PORT_DISCONNECTED,
	VISDN_EVENT_PORT_ACTIVATED,
	VISDN_EVENT_PORT_DEACTIVATED,
	VISDN_EVENT_PORT_ERROR_INDICATION_0,
	VISDN_EVENT_PORT_ERROR_INDICATION_1,
	VISDN_EVENT_PORT_ERROR_INDICATION_2,
	VISDN_EVENT_PORT_ERROR_INDICATION_3,
	VISDN_EVENT_PORT_ERROR_INDICATION_4,
};

extern const char *visdn_event_to_text(enum visdn_event event);

extern int visdn_register_notifier(struct notifier_block *nb);
extern int visdn_unregister_notifier(struct notifier_block *nb);
extern int visdn_call_notifiers(unsigned long val, void *v);

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

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

extern int debug_level;

#endif

#endif

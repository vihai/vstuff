/*
 * vISDN gateway between vISDN's crossconnector and Linux's netdev subsystem
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_NETDEV_H
#define _VISDN_NETDEV_H

#include <linux/if.h>

#define VND_IOC_CREATE	_IOR(0xd0, 0x40, unsigned int)
#define VND_IOC_DESTROY	_IOR(0xd0, 0x41, unsigned int)

struct vnd_create
{
	int protocol;
	char devname[IFNAMSIZ];

	int d_chan;
	int e_chan;
};

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <linux/visdn/core.h>

#define vnd_MODULE_NAME "visdn-netdev"
#define vnd_MODULE_PREFIX vnd_MODULE_NAME ": "
#define vnd_MODULE_DESCR "Netdevice gateway"

#define VND_CHANNEL_SYMLINK "visdn_channel"
#define VND_CHANNEL_SYMLINK_E "visdn_channel_e"

#define VND_CHAN_HASHBITS 8
#define VND_CHAN_HASHSIZE (1 << VND_CHAN_HASHBITS)

enum vnd_netdevice_state
{
	VND_NETDEVICE_STATE_RTNL_HELD = 0,
};

struct vnd_netdevice
{
	struct list_head list_node;

	struct net_device *netdev;
	int type;

	unsigned long state;

	atomic_t refcnt;

	int mtu;

	struct visdn_chan visdn_chan;
	struct visdn_chan visdn_chan_e;

	struct visdn_port *remote_port; 

	struct net_device_stats stats;

	struct work_struct promiscuity_change_work;

	u32 last_crc;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vnd_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vnd_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vnd_debug(format, arg...) do {} while (0)
#endif

#define vnd_msg(level, format, arg...)				\
	printk(level vnd_MODULE_PREFIX				\
		format,						\
		## arg)

#endif

#endif

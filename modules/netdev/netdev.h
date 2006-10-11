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

	char d_chan[80];
	char e_chan[80];
};

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#define vnd_MODULE_NAME "visdn-netdev"
#define vnd_MODULE_PREFIX vnd_MODULE_NAME ": "
#define vnd_MODULE_DESCR "Netdevice gateway"

#define VND_NODE_SYMLINK_D "ks_node_d"
#define VND_NODE_SYMLINK_E "ks_node_e"
#define VND_CONNECTED_NODE_SYMLINK_D "visdn_connected_node_d"
#define VND_CONNECTED_NODE_SYMLINK_E "visdn_connected_node_e"
#define VND_CONNECTED_PORT_SYMLINK "visdn_connected_port"

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

	struct kref kref;

	char name[32];

	struct ks_node ks_node_d;
	struct ks_duplex ks_duplex_d;
	struct ks_link ks_link_d_rx;
	struct ks_link ks_link_d_tx;

	struct ks_node ks_node_e;
	struct ks_duplex ks_duplex_e;
	struct ks_link ks_link_e_rx;
	struct ks_link ks_link_e_tx;

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

#define vnd_debug_nd(nd, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vnd_MODULE_PREFIX		\
			"%s: "					\
			format,					\
			(nd)->name,				\
			## arg)
#else
#define vnd_debug(format, arg...) do {} while (0)
#define vnd_debug_nd(format, arg...) do {} while (0)
#endif

#define vnd_msg(level, format, arg...)				\
	printk(level vnd_MODULE_PREFIX				\
		format,						\
		## arg)

#define vnd_msg_nd(nd, level, format, arg...)			\
	printk(level vnd_MODULE_PREFIX				\
		"%s: "						\
		format,						\
		(nd)->name					\
		## arg)


#endif

#endif

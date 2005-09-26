/*
 * vISDN gateway between vISDN's crossconnector and Linux's netdev subsystem
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_NETDEV_H
#define _VISDN_NETDEV_H

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <visdn.h>

#define vnd_MODULE_NAME "visdn-netdev"
#define vnd_MODULE_PREFIX sb_MODULE_NAME ": "
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
	int index;
	struct hlist_node index_hlist_node;

	struct net_device *netdev;
	int type;

	unsigned long state;

	atomic_t refcnt;

	struct visdn_chan visdn_chan;
	struct visdn_chan visdn_chan_e;

	struct work_struct promiscuity_change_work;
};

#endif

#endif

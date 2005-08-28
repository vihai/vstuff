#ifndef _SOFTPORT_H
#define _SOFTPORT_H

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <visdn.h>

#define vnd_DRIVER_NAME "visdn-netdev"
#define vnd_DRIVER_PREFIX sb_DRIVER_NAME ": "
#define vnd_DRIVER_DESCR "Netdevice gateway"

#define VND_CHAN_HASHBITS 8
#define VND_CHAN_HASHSIZE (1 << VND_CHAN_HASHBITS)

#define to_vnd_chan(visdn_chan) container_of(visdn_chan, struct vnd_chan, visdn_chan)

struct vnd_chan
{
	int index;
	struct hlist_node index_hlist_node;

	struct net_device *netdev;

	struct visdn_chan visdn_chan;
};

#endif

#endif

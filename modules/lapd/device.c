/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "output.h"
#include "tei_mgmt_nt.h"
#include "sock_inline.h"

struct lapd_device *lapd_dev_get_by_name(const char *name)
{
	struct net_device *dev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	dev = dev_get_by_name(name);
#else
	dev = dev_get_by_name(&init_net, name);
#endif
	if (!dev)
		return NULL;

	if (dev->type != ARPHRD_LAPD) {
		dev_put(dev);
		return NULL;
	}

	if (to_lapd_dev(dev))
		return to_lapd_dev(dev);
	else {
		dev_put(dev);
		return NULL;
	}
}

static void lapd_device_up(struct net_device *dev)
{
	struct lapd_device *lapd_device;
	lapd_device = kmalloc(sizeof(*lapd_device), GFP_ATOMIC);
	if (!lapd_device)
		return;

	memset(lapd_device, 0, sizeof(*lapd_device));

	lapd_device->l1_state = LAPD_L1_STATE_UNAVAILABLE;

	if (dev->do_ioctl)
		dev->do_ioctl(dev, NULL, LAPD_DEV_IOC_ACTIVATE);

	spin_lock_init(&lapd_device->out_queue_lock);
	skb_queue_head_init(&lapd_device->out_queue);

	/* TODO FIXME use the correct pointer XXX */
	dev->atalk_ptr = lapd_device;

	lapd_device->dev = dev;
	lapd_dev_get(lapd_device);

	/* FIXME */
	if (dev->flags & IFF_PORTSEL)
		lapd_device->type = LAPD_INTF_TYPE_PRA;
	else
		lapd_device->type = LAPD_INTF_TYPE_BRA;

	/* FIXME */
	if (dev->flags & IFF_NOARP)
		lapd_device->mode = LAPD_INTF_MODE_POINT_TO_POINT;
	else
		lapd_device->mode = LAPD_INTF_MODE_MULTIPOINT;

	if (dev->flags & IFF_ALLMULTI) {
		lapd_device->role = LAPD_INTF_ROLE_NT;

		lapd_device->net_tme = lapd_ntme_alloc(lapd_device);

		lapd_ntme_get(lapd_device->net_tme);
		hlist_add_head(&lapd_device->net_tme->node, &lapd_ntme_hash);
	} else {
		lapd_device->role = LAPD_INTF_ROLE_TE;
		lapd_device->net_tme = NULL;
	}

	/* q.931 SAP */

	lapd_device->q931.k = 7;
	lapd_device->q931.N200 = 3;
	lapd_device->q931.N201 = 260;
	lapd_device->q931.T200 = 1 * HZ;
	lapd_device->q931.T203 = 10 * HZ;

	/* x.25 SAP */

	lapd_device->x25.k = 7;
	lapd_device->x25.N200 = 3;
	lapd_device->x25.N201 = 260;
	lapd_device->x25.T200 = 1 * HZ;
	lapd_device->x25.T203 = 10 * HZ;
}

static void lapd_kill_by_device(struct lapd_device *dev)
{
	struct sock *sk;
	struct hlist_node *node;

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, lapd_get_hash(dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == dev) {

printk(KERN_DEBUG "Socket %p set to shutdown\n", sk);

			bh_lock_sock(sk);

			sk->sk_shutdown = SHUTDOWN_MASK;
			sk->sk_err = ENETDOWN;

			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_state_change(sk);

			bh_unlock_sock(sk);
		}
	}
	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_device_down(struct net_device *dev)
{
	struct lapd_device *lapd_device = to_lapd_dev(dev);

	lapd_kill_by_device(lapd_device);

	if (lapd_device) {

		lapd_out_queue_drop(lapd_device);

		if (lapd_device->net_tme) {
			hlist_del(&lapd_device->net_tme->node);
			lapd_ntme_put(lapd_device->net_tme);

			lapd_ntme_put(lapd_device->net_tme);
			lapd_device->net_tme = NULL;
		}

		dev->atalk_ptr = NULL;
		dev_put(dev);
		kfree(lapd_device);
	}
}

int lapd_device_event(
	struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (dev->type != ARPHRD_LAPD)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		lapd_msg(KERN_DEBUG, "NETDEV_UP %s\n", dev->name);
		lapd_device_up(dev);
	break;

	case NETDEV_DOWN:
		lapd_msg(KERN_DEBUG, "NETDEV_DOWN %s\n", dev->name);
		lapd_device_down(dev);
	break;

	case NETDEV_UNREGISTER:
		lapd_msg(KERN_DEBUG, "NETDEV_UNREGISTER %s\n", dev->name);
	break;

	case NETDEV_CHANGE:
		lapd_msg(KERN_DEBUG, "NETDEV_CHANGE %s\n", dev->name);
	break;
	}

	return NOTIFY_DONE;
}

/*
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "tei_mgmt_nt.h"

static void lapd_device_up(struct net_device *dev)
{
	struct lapd_device *lapd_device;
	lapd_device = kmalloc(sizeof(*lapd_device), GFP_ATOMIC);
	if (!lapd_device)
		return;

	memset(lapd_device, 0x00, sizeof(*lapd_device));

	// TODO FIXME use the correct pointer
	dev->atalk_ptr = lapd_device;

	lapd_device->dev = dev;
	dev_hold(dev);

	if (dev->flags & IFF_ALLMULTI) {
		lapd_device->net_tme = lapd_ntme_alloc(dev);

		hlist_add_head(&lapd_device->net_tme->node, &lapd_ntme_hash);
		lapd_ntme_hold(lapd_device->net_tme);
	} else {
		lapd_device->net_tme = NULL;
	}

	// q.931 SAP

	lapd_device->q931.k = 7;
	lapd_device->q931.N200 = 3;
	lapd_device->q931.N201 = 260;
	lapd_device->q931.T200 = 1 * HZ;
	lapd_device->q931.T203 = 10 * HZ;

	// x.25 SAP

	lapd_device->x25.k = 7;
	lapd_device->x25.N200 = 3;
	lapd_device->x25.N201 = 260;
	lapd_device->x25.T200 = 1 * HZ;
	lapd_device->x25.T203 = 10 * HZ;
}

static void lapd_kill_by_device(struct net_device *dev)
{
	struct sock *sk;
	struct hlist_node *node;

	write_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		if (lo->dev == dev) {
			sk->sk_state = TCP_CLOSING;
			sk->sk_shutdown = SHUTDOWN_MASK;
			sk->sk_err = ENETDOWN;

			if (!sock_flag(sk, SOCK_DEAD)) {
				sk->sk_state_change(sk);
				sock_set_flag(sk, SOCK_DEAD);
			}
		}
	}
	write_unlock_bh(&lapd_hash_lock);
}

static void lapd_device_down(struct net_device *dev)
{
	struct lapd_device *lapd_device =
		lapd_dev(dev);

	lapd_kill_by_device(dev);

	if (lapd_device) {

		if (lapd_device->net_tme) {
			hlist_del(&lapd_device->net_tme->node);
			lapd_ntme_put(lapd_device->net_tme);

			lapd_ntme_put(lapd_device->net_tme);
			lapd_device->net_tme = NULL;
		}

		dev_put(dev);
		kfree(lapd_device);
		dev->atalk_ptr = NULL;
	}
}

int lapd_device_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (dev->type != ARPHRD_LAPD)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		lapd_printk(KERN_DEBUG, "NETDEV_UP %s\n", dev->name);
		lapd_device_up(dev);
	break;

	case NETDEV_DOWN:
		lapd_printk(KERN_DEBUG, "NETDEV_DOWN %s\n", dev->name);
		lapd_device_down(dev);
	break;

	case NETDEV_UNREGISTER:
		lapd_printk(KERN_DEBUG, "NETDEV_UNREGISTER %s\n", dev->name);
	break;

	case NETDEV_CHANGE:
		lapd_printk(KERN_DEBUG, "NETDEV_CHANGE %s\n", dev->name);

		// PH-ACTIVATE-INDICATION
		// PH-DEACTIVATE-INDICATION
	break;
	}

	return NOTIFY_DONE;
}
/*
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "tei_mgmt.h"

static void lapd_device_up(struct net_device *dev)
{
	struct lapd_dev *lapd_dev;

	printk(KERN_DEBUG "lapd: device %s up\n", dev->name);

	lapd_dev = kmalloc(sizeof(*lapd_dev), GFP_ATOMIC);
	if (!lapd_dev) {
		return;
	}

	memset(lapd_dev, 0x00, sizeof(*lapd_dev));

	// TODO FIXME use the correct pointer
	dev->atalk_ptr = lapd_dev;

	lapd_dev->dev = dev;
	dev_hold(dev);

	lapd_dev->net_tme = lapd_net_tei_mgmt_entity_alloc();

	// q.931 SAP

	lapd_dev->q931.k = 1;
	lapd_dev->q931.N200 = 3;
	lapd_dev->q931.N201 = 260;
	lapd_dev->q931.T200 = 1 * HZ;
	lapd_dev->q931.T203 = 10 * HZ;

	// x.25 SAP

	lapd_dev->x25.k = 1;
	lapd_dev->x25.N200 = 3;
	lapd_dev->x25.N201 = 260;
	lapd_dev->x25.T200 = 1 * HZ;
	lapd_dev->x25.T203 = 10 * HZ;
}

static void lapd_device_down(struct net_device *dev)
{
	struct lapd_dev *lapd_dev = (struct lapd_dev *)dev_atalk_ptr;

	printk(KERN_DEBUG "lapd: device %s down\n", dev->name);

	if (lapd_dev) {
		lapd_net_tei_mgmt_entity_put(lapd_dev->net_tme);
		lapd_dev->net_tme = NULL;

		dev_put(dev);
		kfree(lapd_dev);
		dev->atalk_ptr = NULL;
	}
}

int lapd_device_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (dev->type != ARPHDR_ISDN_DCHAN)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		lapd_device_up(dev);
	break;

	case NETDEV_DOWN:
		lapd_device_down(dev);
	break;
	}

	return NOTIFY_DONE;
}

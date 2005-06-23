/*
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 * Please read the README file for important infos.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include "visdn.h"
#include "visdn_mod.h"
#include "chan.h"
#include "port.h"

#include "../lapd/lapd.h"

static ssize_t visdn_chan_show_host_bus(
	struct device *device,
	char *buf)
{
	BUG_ON(!device);
	BUG_ON(!device->parent);
	BUG_ON(!device->parent->parent);
	BUG_ON(!device->parent->parent->bus);

	return snprintf(buf, PAGE_SIZE, "%s\n", device->parent->parent->bus->name);
}

static DEVICE_ATTR(host_bus, S_IRUGO | S_IWUSR,
		visdn_chan_show_host_bus,
		NULL);

static ssize_t visdn_chan_show_host_bus_id(
	struct device *device,
	char *buf)
{
	BUG_ON(!device);
	BUG_ON(!device->parent);
	BUG_ON(!device->parent->parent);

	return snprintf(buf, PAGE_SIZE, "%s\n", device->parent->parent->bus_id);
}

static DEVICE_ATTR(host_bus_id, S_IRUGO | S_IWUSR,
		visdn_chan_show_host_bus_id,
		NULL);

static ssize_t visdn_chan_show_port_name(
	struct device *device,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
		to_visdn_port(device->parent)->port_name);
}

static DEVICE_ATTR(port_name, S_IRUGO | S_IWUSR,
		visdn_chan_show_port_name,
		NULL);

static ssize_t visdn_chan_show_role(
	struct device *device,
	char *buf)
{
	switch(to_visdn_chan(device)->role) {
		case VISDN_CHAN_ROLE_B:
			return snprintf(buf, PAGE_SIZE, "B\n");
		case VISDN_CHAN_ROLE_D:
			return snprintf(buf, PAGE_SIZE, "D\n");
		case VISDN_CHAN_ROLE_E:
			return snprintf(buf, PAGE_SIZE, "E\n");
		case VISDN_CHAN_ROLE_S:
			return snprintf(buf, PAGE_SIZE, "S\n");
		case VISDN_CHAN_ROLE_Q:
			return snprintf(buf, PAGE_SIZE, "Q\n");
		default:
			return snprintf(buf, PAGE_SIZE, "UNKNOWN!\n");
	}
}

static ssize_t visdn_chan_store_role(
	struct device *device,
	const char *buf,
	size_t count)
{
	return -EINVAL;
}

static DEVICE_ATTR(role, S_IRUGO | S_IWUSR,
		visdn_chan_show_role,
		visdn_chan_store_role);

static ssize_t visdn_chan_show_roles(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);
	ssize_t curpos = 0;

	if (chan->roles & VISDN_CHAN_ROLE_B)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "B\n");

	if (chan->roles & VISDN_CHAN_ROLE_D)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "D\n");

	if (chan->roles & VISDN_CHAN_ROLE_E)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "E\n");

	if (chan->roles & VISDN_CHAN_ROLE_S)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "S\n");

	if (chan->roles & VISDN_CHAN_ROLE_Q)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "Q\n");

	return curpos;
}

static ssize_t visdn_chan_store_roles(
	struct device *device,
	const char *buf,
	size_t count)
{
	return -EINVAL;
}

static DEVICE_ATTR(roles, S_IRUGO | S_IWUSR,
		visdn_chan_show_roles,
		visdn_chan_store_roles);

static ssize_t visdn_chan_show_speed(
	struct device *device,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		to_visdn_chan(device)->speed);
}

static ssize_t visdn_chan_store_speed(
	struct device *device,
	const char *buf,
	size_t count)
{
	//chan->netdev->dev_addr[0] = 0x01;

	return -EINVAL;
}

static DEVICE_ATTR(speed, S_IRUGO | S_IWUSR,
		visdn_chan_show_speed,
		visdn_chan_store_speed);


static ssize_t visdn_chan_show_protocol(
	struct device *device,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		to_visdn_chan(device)->protocol);
}

static ssize_t visdn_chan_store_protocol(
	struct device *device,
	const char *buf,
	size_t count)
{
	//chan->netdev->dev_addr[0] = 0x01;

	return -EINVAL;
}

static DEVICE_ATTR(protocol, S_IRUGO | S_IWUSR,
		visdn_chan_show_protocol,
		visdn_chan_store_protocol);

static ssize_t visdn_chan_show_flags(
	struct device *device,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		to_visdn_chan(device)->flags);
}

static ssize_t visdn_chan_store_flags(
	struct device *device,
	const char *buf,
	size_t count)
{
	//chan->netdev->dev_addr[0] = 0x01;

	return -EINVAL;
}

static DEVICE_ATTR(flags, S_IRUGO | S_IWUSR,
		visdn_chan_show_flags,
		visdn_chan_store_flags);

static int visdn_chan_hotplug(struct device *cd, char **envp,
	int num_envp, char *buf, int size)
{
//	struct visdn_chan *visdn_chan = to_visdn_chan(cd);

	envp[0] = NULL;

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_hotplug called\n");

	return 0;
}

static void visdn_chan_release(struct device *cd)
{
//	struct visdn_chan *visdn_chan =
//		container_of(cd, struct visdn_chan, class_dev);

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_release called\n");

	// kfree ??
}

struct visdn_chan *visdn_chan_alloc(void)
{
	struct visdn_chan *visdn_chan;

	visdn_chan = kmalloc(sizeof(*visdn_chan), GFP_KERNEL);
	if (!visdn_chan)
		return NULL;

	memset(visdn_chan, 0x00, sizeof(*visdn_chan));

	return visdn_chan;
}
EXPORT_SYMBOL(visdn_chan_alloc);

void visdn_chan_init(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_ops *ops)
{
	BUG_ON(!visdn_chan);
	BUG_ON(!ops);

	memset(visdn_chan, 0x00, sizeof(*visdn_chan));
	visdn_chan->ops = ops;
}
EXPORT_SYMBOL(visdn_chan_init);

static int visdn_netdev_open(struct net_device *netdev)
{
	struct visdn_chan *chan = netdev->priv;

	return chan->ops->open(chan, 0);
}

static int visdn_netdev_stop(struct net_device *netdev)
{
	struct visdn_chan *chan = netdev->priv;

	return chan->ops->close(chan);
}

static int visdn_netdev_frame_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct visdn_chan *chan = netdev->priv;

	netdev->trans_start = jiffies;

	return chan->ops->frame_xmit(chan, skb);
}

static struct net_device_stats *visdn_netdev_get_stats(struct net_device *netdev)
{
	struct visdn_chan *chan = netdev->priv;

	return chan->ops->get_stats(chan);
}

static void visdn_netdev_set_multicast_list(struct net_device *netdev)
{
//	struct visdn_chan *chan = netdev->priv;

//        if(netdev->flags & IFF_PROMISC && !port->echo_enabled) {
//	}
}

static int visdn_netdev_do_ioctl(struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	struct visdn_chan *chan = netdev->priv;

	return chan->ops->do_ioctl(chan, ifr, cmd);
}

static void visdn_frame_input_error(struct visdn_chan *chan, int code)
{
	if(0) { //PPP
		ppp_input_error(&chan->ppp_chan, code);
	}
}

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb)
{

	if(0) { //PPP
		ppp_input(&chan->ppp_chan, skb);
	} else {
		skb->protocol = htons(chan->protocol);
		skb->dev = chan->netdev;
		skb->pkt_type = PACKET_HOST;

/*		// Oh... this is the echo channel... redirect to D
		// channel's netdev
		if (fdchan->id == E) {
			skb->protocol = htons(port->chans[D].protocol);
			skb->dev = port->chans[D].netdev;
			skb->pkt_type = PACKET_OTHERHOST;
		} else {
			skb->protocol = htons(fdchan->protocol);
			skb->dev = fdchan->netdev;
			skb->pkt_type = PACKET_HOST;
		}*/

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		return netif_rx(skb);
	}
}
EXPORT_SYMBOL(visdn_frame_rx);

static int hfc_ppp_start_xmit(
	struct ppp_channel *ppp_chan,
	struct sk_buff *skb)
{
	return -EINVAL;
}

static int hfc_ppp_ioctl(
	struct ppp_channel *ppp_chan,
	unsigned int cmd,
	unsigned long arg)
{
	return -EINVAL;
}

static struct ppp_channel_ops hfc_ppp_ops =
{
	start_xmit: hfc_ppp_start_xmit,
	ioctl: hfc_ppp_ioctl,
};


int visdn_chan_create_netdev(
	struct visdn_chan *chan,
	struct visdn_port *port)
{
	int err;

	chan->netdev = alloc_netdev(0, "visdn%dd", setup_lapd);
	if(!chan->netdev) {
		printk(KERN_CRIT
			"net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	chan->netdev->priv = chan;
	chan->netdev->open = visdn_netdev_open;
	chan->netdev->stop = visdn_netdev_stop;
	chan->netdev->hard_start_xmit = visdn_netdev_frame_xmit;
	chan->netdev->get_stats = visdn_netdev_get_stats;
	chan->netdev->set_multicast_list = visdn_netdev_set_multicast_list;
	chan->netdev->do_ioctl = visdn_netdev_do_ioctl;
	chan->netdev->features = NETIF_F_NO_CSUM;

	chan->netdev->mtu = 200;
//	chan->netdev->mtu = chan->tx.fifo_size;

	memset(chan->netdev->dev_addr, 0x00, sizeof(chan->netdev->dev_addr));

	SET_MODULE_OWNER(chan->netdev);
	SET_NETDEV_DEV(chan->netdev, &chan->device);

	chan->netdev->irq = 0;
//	chan->netdev->irq = card->pcidev->irq;
	chan->netdev->base_addr = 0;
//	chan->netdev->base_addr = card->io_bus_mem;

	err = register_netdev(chan->netdev);
	if(err) {
		printk(KERN_CRIT
			"Cannot register net device %s, aborting.\n",
			chan->netdev->name);
		goto err_register_netdev;
	}

//	err = sysfs_create_link(&chan->netdev->class_dev.kobj, &chan->class_dev.kobj, "visdn_chan");

	return 0;

	unregister_netdev(chan->netdev);
err_register_netdev:
	free_netdev(chan->netdev);
err_alloc_netdev:

	return 0;
}

void visdn_chan_destroy_netdev(struct visdn_chan *chan)
{
	unregister_netdev(chan->netdev);
	free_netdev(chan->netdev);
}

static int visdn_suspend(struct device *device, pm_message_t state)
{
	printk(KERN_INFO "######### visdn_suspend()\n");

	return -EINVAL;
}

static int visdn_resume(struct device *dev)
{
	printk(KERN_INFO "######### visdn_resume()\n");

	return -EINVAL;
}

struct bus_type visdn_bus_type = {
	.name           = "visdn",
	.suspend        = visdn_suspend,
	.resume         = visdn_resume,
	.hotplug	= visdn_chan_hotplug,
};


int visdn_chan_register(
	struct visdn_chan *chan,
	const char *name,
	struct visdn_port *port)
{
	int err;

	BUG_ON(!chan);
	BUG_ON(!name);
	BUG_ON(!port);

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_register(%s) called\n", name);

	chan->device.parent = &port->device;

	snprintf(chan->device.bus_id, sizeof(chan->device.bus_id),
		"%d.%s", port->index, name);

	chan->device.bus = &visdn_bus_type;
	chan->device.driver = port->device.driver;
	chan->device.driver_data = NULL;
	chan->device.release = visdn_chan_release;

	err = device_register(&chan->device);
	if (err < 0)
		goto err_device_register;

	if (chan->device.parent->parent->bus) {
		err = device_create_file(
				&chan->device,
				&dev_attr_host_bus);
		if (err < 0)
			goto err_device_create_file;

		err = device_create_file(
				&chan->device,
				&dev_attr_host_bus_id);
		if (err < 0)
			goto err_device_create_file;
	}

	err = device_create_file(
			&chan->device,
			&dev_attr_port_name);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_role);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_roles);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_protocol);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_speed);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_flags);
	if (err < 0)
		goto err_device_create_file;

	kobject_get(&port->device.kobj);
	chan->port = port;

//	sysfs_create_link(&class_dev->kobj, &port->class_dev.kobj, "port");

	if (chan->role == VISDN_CHAN_ROLE_D &&
	    chan->protocol == ETH_P_LAPD) {
		visdn_chan_create_netdev(chan, port);
	}

	return 0;

err_device_create_file:
	device_unregister(&chan->device);
err_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_chan_register);

void visdn_chan_unregister(
	struct visdn_chan *visdn_chan)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_unregister called\n");

	if (visdn_chan->netdev) {
		visdn_chan_destroy_netdev(visdn_chan);
	}

	kobject_put(&visdn_chan->port->device.kobj);

	device_unregister(&visdn_chan->device);
}
EXPORT_SYMBOL(visdn_chan_unregister);

int visdn_chan_modinit(void)
{
	int err;

	err = bus_register(&visdn_bus_type);
	if (err < 0)
		goto err_bus_register;

	return 0;

	bus_unregister(&visdn_bus_type);
err_bus_register:

	return err;
}

void visdn_chan_modexit(void)
{
	bus_unregister(&visdn_bus_type);
}

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/list.h>

#include <visdn.h>
#include <lapd.h>

#include "netdev.h"

static dev_t vnd_first_dev;
static struct cdev vnd_cdev;
static struct class_device vnd_control_class_dev;

struct hlist_head vnd_netdevice_index_hash[VND_CHAN_HASHSIZE];

static inline struct hlist_head *vnd_netdevice_index_get_hash(int index)
{
	return &vnd_netdevice_index_hash[index & (VND_CHAN_HASHSIZE - 1)];
}

struct vnd_netdevice *__vnd_netdevice_get_by_index(int index)
{
	struct hlist_node *t;
	struct vnd_netdevice *vnd_netdevice;

	hlist_for_each_entry(vnd_netdevice, t, vnd_netdevice_index_get_hash(index),
			index_hlist_node) {
		if (vnd_netdevice->index == index)
			return vnd_netdevice;
	}

	return NULL;
}

static int vnd_netdevice_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__vnd_netdevice_get_by_index(index))
			return index;
	}
}

struct visdn_port vnd_port;

static void vnd_chan_release(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	kfree(netdevice);
}

static int vnd_chan_open(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vnd_open()\n");

	return 0;
}

static int vnd_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vnd_close()\n");

	return 0;
}

static int vnd_chan_frame_xmit(struct visdn_chan *visdn_chan, struct sk_buff *skb)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	skb->protocol = htons(ETH_P_LAPD); // FIXME chan->protocol);
	skb->dev = netdevice->netdev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (visdn_chan == &netdevice->visdn_chan) {
		skb->pkt_type = PACKET_HOST;
	} else {
		skb->pkt_type = PACKET_OTHERHOST;
	}

	netdevice->netdev->last_rx = jiffies;

	return netif_rx(skb);
}

static int vnd_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;
	int err;

	if (visdn_chan2->connected_chan ||
	    visdn_chan->connected_chan)
		return -EBUSY;

	printk(KERN_INFO "%s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	if (visdn_chan == &netdevice->visdn_chan) {
		err = register_netdev(netdevice->netdev);
		if(err) {
			printk(KERN_CRIT
				"Cannot register net device %s, aborting.\n",
				netdevice->netdev->name);
			goto err_register_netdev;
		}
	}

	return 0;

err_register_netdev:

	return err;
}

static int vnd_chan_disconnect(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	if (visdn_chan == &netdevice->visdn_chan)
		unregister_netdev(netdevice->netdev);

	if (!visdn_chan->connected_chan)
		return 0;

	printk(KERN_INFO "%s disconnected from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	return 0;
}

void vnd_chan_stop_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	netif_stop_queue(netdevice->netdev);
}

void vnd_chan_start_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	netif_start_queue(netdevice->netdev);
}

void vnd_chan_wake_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->priv;

	netif_wake_queue(netdevice->netdev);
}

struct visdn_chan_ops vnd_chan_ops = {
	.release		= vnd_chan_release,
	.open			= vnd_chan_open,
	.close			= vnd_chan_close,
	.frame_xmit		= vnd_chan_frame_xmit,
	.frame_input_error	= NULL,
	.get_stats		= NULL,
	.do_ioctl		= NULL,

	.connect_to     	= vnd_chan_connect_to,
	.disconnect		= vnd_chan_disconnect,

	.samples_read   	= NULL,
	.samples_write  	= NULL,

	.stop_queue		= vnd_chan_stop_queue,
	.start_queue		= vnd_chan_start_queue,
	.wake_queue		= vnd_chan_wake_queue,
};

static int vnd_netdev_open(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	printk(KERN_INFO "vnd_netdev_open()\n");

	return visdn_open(netdevice->visdn_chan.connected_chan);
}

static int vnd_netdev_stop(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	printk(KERN_INFO "vnd_netdev_stop()\n");

	if (netdevice->visdn_chan.connected_chan)
		visdn_close(netdevice->visdn_chan.connected_chan);

	if (netdevice->visdn_chan_e.open &&
	    netdevice->visdn_chan_e.connected_chan)
		visdn_close(netdevice->visdn_chan_e.connected_chan);

	return 0;
}

static int vnd_netdev_frame_xmit(
	struct sk_buff *skb,
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	netdev->trans_start = jiffies;

	if (netdevice->visdn_chan.connected_chan &&
	    netdevice->visdn_chan.connected_chan->ops->frame_xmit)
		return netdevice->visdn_chan.connected_chan->ops->frame_xmit(
				netdevice->visdn_chan.connected_chan, skb);

	return -EOPNOTSUPP;
}

static struct net_device_stats vnd_dummy_stats = { };

static struct net_device_stats *vnd_netdev_get_stats(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	if (netdevice->visdn_chan.connected_chan &&
	    netdevice->visdn_chan.connected_chan->ops->frame_xmit)
		return netdevice->visdn_chan.connected_chan->ops->get_stats(
				netdevice->visdn_chan.connected_chan);

	return &vnd_dummy_stats;
}

static void vnd_netdev_set_multicast_list(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	if (netdevice->visdn_chan_e.connected_chan) {

		if((netdev->flags & IFF_PROMISC) &&
		   !netdevice->visdn_chan_e.connected_chan->open) {

			visdn_open(netdevice->visdn_chan_e.connected_chan);

		} else if(!(netdev->flags & IFF_PROMISC) &&
		          netdevice->visdn_chan_e.connected_chan->open) {

			visdn_close(netdevice->visdn_chan_e.connected_chan);
		}
	}
}

static int vnd_netdev_do_ioctl(struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	if (netdevice->visdn_chan.connected_chan->ops->do_ioctl)
		return netdevice->visdn_chan.connected_chan->ops->do_ioctl(
				netdevice->visdn_chan.connected_chan, ifr, cmd);
	else
		return -EOPNOTSUPP;
}

static int vnd_read_done = FALSE;

int vnd_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	vnd_read_done = FALSE;

	return 0;
}

int vnd_cdev_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t vnd_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err;

	if (vnd_read_done)
		return 0;

	struct vnd_netdevice *netdevice = NULL;
	netdevice = kmalloc(sizeof(*netdevice), GFP_KERNEL);
	if (!netdevice) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(netdevice, 0x00, sizeof(*netdevice));

	netdevice->index = vnd_netdevice_new_index();
	hlist_add_head(&netdevice->index_hlist_node,
			vnd_netdevice_index_get_hash(netdevice->index));

	// Main chan
	visdn_chan_init(&netdevice->visdn_chan, &vnd_chan_ops);

	netdevice->visdn_chan.priv = netdevice;
	netdevice->visdn_chan.autoopen = FALSE;
	netdevice->visdn_chan.speed = 0;
	netdevice->visdn_chan.role = VISDN_CHAN_ROLE_D;
	netdevice->visdn_chan.roles = VISDN_CHAN_ROLE_D;

	netdevice->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_HDLC |
					     VISDN_CHAN_FRAMING_MTP;
	netdevice->visdn_chan.framing_preferred = 0;

	netdevice->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB;
	netdevice->visdn_chan.bitorder_preferred = 0;

	// E-chan
	visdn_chan_init(&netdevice->visdn_chan_e, &vnd_chan_ops);

	netdevice->visdn_chan_e.priv = netdevice;
	netdevice->visdn_chan_e.autoopen = FALSE;
	netdevice->visdn_chan_e.speed = 0;
	netdevice->visdn_chan_e.role = VISDN_CHAN_ROLE_D;
	netdevice->visdn_chan_e.roles = VISDN_CHAN_ROLE_D;

	netdevice->visdn_chan_e.framing_supported = VISDN_CHAN_FRAMING_HDLC |
					     VISDN_CHAN_FRAMING_MTP;
	netdevice->visdn_chan_e.framing_preferred = 0;

	netdevice->visdn_chan_e.bitorder_supported = VISDN_CHAN_BITORDER_LSB;
	netdevice->visdn_chan_e.bitorder_preferred = 0;

	char chanid[60];

	snprintf(chanid, sizeof(chanid), "%d", netdevice->index);
	err = visdn_chan_register(&netdevice->visdn_chan, chanid, &vnd_port);
	if (err < 0)
		goto err_visdn_chan_register;

	snprintf(chanid, sizeof(chanid), "%de", netdevice->index);
	err = visdn_chan_register(&netdevice->visdn_chan_e, chanid, &vnd_port);
	if (err < 0)
		goto err_visdn_chan_e_register;

	char ifname[60];
	snprintf(ifname, sizeof(ifname), "visdn%dd", netdevice->index);

	netdevice->netdev = alloc_netdev(0, ifname, setup_lapd);
	if(!netdevice->netdev) {
		printk(KERN_CRIT
			"net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	netdevice->netdev->priv = netdevice;
	netdevice->netdev->open = vnd_netdev_open;
	netdevice->netdev->stop = vnd_netdev_stop;
	netdevice->netdev->hard_start_xmit = vnd_netdev_frame_xmit;
	netdevice->netdev->get_stats = vnd_netdev_get_stats;
	netdevice->netdev->set_multicast_list = vnd_netdev_set_multicast_list;
	netdevice->netdev->do_ioctl = vnd_netdev_do_ioctl;
	netdevice->netdev->features = NETIF_F_NO_CSUM;

	netdevice->netdev->mtu = 200;
//	netdevice->netdev->mtu = netdevice->tx.fifo_size;

	memset(netdevice->netdev->dev_addr, 0x00, sizeof(netdevice->netdev->dev_addr));

	SET_MODULE_OWNER(netdevice->netdev);
	SET_NETDEV_DEV(netdevice->netdev, &netdevice->visdn_chan.device);

	netdevice->netdev->irq = 0;
	netdevice->netdev->base_addr = 0;

	char busid[30];
	int len = snprintf(busid, sizeof(busid), "%s\n",
		netdevice->visdn_chan.device.bus_id);

	if (copy_to_user(buf, busid, len)) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	vnd_read_done = TRUE;

	return len;

err_copy_to_user:
	visdn_chan_unregister(&netdevice->visdn_chan_e);
err_visdn_chan_e_register:
	visdn_chan_unregister(&netdevice->visdn_chan);
err_visdn_chan_register:
	free_netdev(netdevice->netdev);
err_alloc_netdev:
	kfree(netdevice);
//	hlist_del(); FIXME
err_kmalloc:

	return err;
}

struct file_operations vnd_fops =
{
	.owner		= THIS_MODULE,
	.read		= vnd_cdev_read,
	.write		= NULL,
	.ioctl		= NULL,
	.open		= vnd_cdev_open,
	.release	= vnd_cdev_release,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops vnd_port_ops = {
	.enable		= NULL,
	.disable	= NULL,
};

#ifdef NO_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vnd_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vnd_init_module(void)
{
	int err;

	printk(KERN_INFO vnd_MODULE_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(vnd_netdevice_index_hash); i++) {
		INIT_HLIST_HEAD(&vnd_netdevice_index_hash[i]);
	}

	visdn_port_init(&vnd_port, &vnd_port_ops);
	err = visdn_port_register(&vnd_port,
		"netdev", "netdev",
		&visdn_system_device);
	if (err < 0)
		goto err_visdn_port_register;

	err = alloc_chrdev_region(&vnd_first_dev, 0, 1, vnd_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vnd_cdev, &vnd_fops);
	vnd_cdev.owner = THIS_MODULE;

	err = cdev_add(&vnd_cdev, vnd_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	class_device_initialize(&vnd_control_class_dev);
	vnd_control_class_dev.class = &visdn_system_class;
	vnd_control_class_dev.class_data = NULL;
	vnd_control_class_dev.dev = &vnd_port.device;
#ifndef NO_CLASS_DEV_DEVT
	vnd_control_class_dev.devt = vnd_first_dev;
#endif
	snprintf(vnd_control_class_dev.class_id,
		sizeof(vnd_control_class_dev.class_id),
		"netdev-control");

	err = class_device_register(&vnd_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifdef NO_CLASS_DEV_DEVT
	class_device_create_file(
		&vnd_control_class_dev,
		&class_device_attr_dev);
#endif


	return 0;

	class_device_del(&vnd_control_class_dev);
err_control_class_device_register:
	cdev_del(&vnd_cdev);
err_cdev_add:
	unregister_chrdev_region(vnd_first_dev, 1);
err_register_chrdev:
	visdn_port_unregister(&vnd_port);
err_visdn_port_register:

	return err;
}

module_init(vnd_init_module);

static void __exit vnd_module_exit(void)
{
	struct hlist_node *t;
	struct vnd_netdevice *netdevice;

	int i;
	for (i=0; i<ARRAY_SIZE(vnd_netdevice_index_hash); i++) {
		hlist_for_each_entry(netdevice, t, &vnd_netdevice_index_hash[i],
				index_hlist_node) {

			visdn_chan_unregister(&netdevice->visdn_chan);
			visdn_chan_unregister(&netdevice->visdn_chan_e);

			free_netdev(netdevice->netdev);
		}
	}

	class_device_del(&vnd_control_class_dev);

	cdev_del(&vnd_cdev);

	unregister_chrdev_region(vnd_first_dev, 1);

	visdn_port_unregister(&vnd_port);

	printk(KERN_INFO vnd_MODULE_DESCR " unloaded\n");
}

module_exit(vnd_module_exit);

MODULE_DESCRIPTION(vnd_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

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
#include <cxc.h>
#include <cxc_internal.h>

#include "netdev.h"

static dev_t vnd_first_dev;
static struct cdev vnd_cdev;
static struct class_device vnd_control_class_dev;

static struct hlist_head vnd_netdevice_index_hash[VND_CHAN_HASHSIZE];

static inline struct hlist_head *vnd_netdevice_index_get_hash(int index)
{
	return &vnd_netdevice_index_hash[index & (VND_CHAN_HASHSIZE - 1)];
}

static struct vnd_netdevice *__vnd_netdevice_get_by_index(int index)
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

static struct visdn_port vnd_port;

static inline void vnd_netdevice_get(
	struct vnd_netdevice *netdevice)
{
	atomic_inc(&netdevice->refcnt);
}

static inline void vnd_netdevice_put(
	struct vnd_netdevice *netdevice)
{
#if 0
	printk(KERN_INFO "vnd_netdevice_put ref=%d\n",
		atomic_read(&netdevice->refcnt) - 1);
	dump_stack();
#endif

	if (atomic_dec_and_test(&netdevice->refcnt)) {
		if (netdevice->netdev)
			free_netdev(netdevice->netdev);

		kfree(netdevice);
	}
}

static void vnd_chan_release(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_open(struct visdn_chan *visdn_chan)
{
	printk(KERN_DEBUG "vnd_open()\n");

	return 0;
}

static int vnd_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_DEBUG "vnd_close()\n");

	return 0;
}

static int vnd_chan_frame_xmit(struct visdn_chan *visdn_chan, struct sk_buff *skb)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	skb->protocol = htons(ETH_P_LAPD);
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
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;
	int err;

	printk(KERN_DEBUG "%s connected to %s\n",
		visdn_chan->cxc_id,
		visdn_chan2->cxc_id);

	if (visdn_chan == &netdevice->visdn_chan) {
		err = register_netdev(netdevice->netdev);
		if(err) {
			printk(KERN_CRIT
				"Cannot register net device %s, aborting.\n",
				netdevice->netdev->name);
			goto err_register_netdev;
		}

		sysfs_create_link(
			&netdevice->netdev->class_dev.kobj,
			&netdevice->visdn_chan.kobj,
			"visdn_channel");
	}

	return 0;

err_register_netdev:

	return err;
}

static int vnd_chan_disconnect(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	if (visdn_chan == &netdevice->visdn_chan) {
		sysfs_remove_link(
			&netdevice->netdev->class_dev.kobj,
			"visdn_channel");

		unregister_netdev(netdevice->netdev);
	}

	printk(KERN_DEBUG "%s disconnected\n",
		visdn_chan->cxc_id);

	return 0;
}

static int vnd_chan_update_parameters(
        struct visdn_chan *visdn_chan,
        struct visdn_chan_pars *pars)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	if (pars->mtu != visdn_chan->pars.mtu) {
		// We must set visdn_chan->pars.mtu before calling dev_set_mtu
		// because mtu_changed() callback will check for it

		visdn_chan->pars.mtu = pars->mtu;

		dev_set_mtu(netdevice->netdev, pars->mtu);
	}

        memcpy(&visdn_chan->pars, pars, sizeof(visdn_chan->pars));

        return 0;
}

static void vnd_chan_stop_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	netif_stop_queue(netdevice->netdev);
}

static void vnd_chan_start_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	netif_start_queue(netdevice->netdev);
}

static void vnd_chan_wake_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	netif_wake_queue(netdevice->netdev);
}

struct visdn_chan_ops vnd_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_chan_release,
	.open			= vnd_chan_open,
	.close			= vnd_chan_close,
	.frame_xmit		= vnd_chan_frame_xmit,
	.frame_input_error	= NULL,
	.get_stats		= NULL,

	.connect_to     	= vnd_chan_connect_to,
	.disconnect		= vnd_chan_disconnect,

	.update_parameters	= vnd_chan_update_parameters,

	.samples_read   	= NULL,
	.samples_write  	= NULL,

	.stop_queue		= vnd_chan_stop_queue,
	.start_queue		= vnd_chan_start_queue,
	.wake_queue		= vnd_chan_wake_queue,
};

static int vnd_netdev_open(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;
	struct visdn_chan *chan = &netdevice->visdn_chan;
	int err;

	printk(KERN_DEBUG "vnd_netdev_open()\n");

	err = visdn_pass_open(chan);

	if (err == VISDN_CHAN_OPEN_RENEGOTIATE) {
		struct visdn_chan *dst;
		dst = visdn_cxc_get_by_src(&visdn_int_cxc.cxc, chan);
		if (!dst) {
			err = -ENODEV;
			goto err_no_dst;
		}

		printk(KERN_DEBUG "Driver asked to renegotiate parameters\n");

		visdn_negotiate_parameters(chan, dst);

		err = 0;

		visdn_chan_put(dst);
err_no_dst:
		;
	}

	return err;
}

static int vnd_netdev_stop(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	printk(KERN_DEBUG "vnd_netdev_stop()\n");

	visdn_pass_close(&netdevice->visdn_chan_e);

	return visdn_pass_close(&netdevice->visdn_chan);
}

static int vnd_netdev_frame_xmit(
	struct sk_buff *skb,
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	netdev->trans_start = jiffies;

	return visdn_pass_frame_xmit(&netdevice->visdn_chan, skb);
}

static struct net_device_stats *vnd_netdev_get_stats(
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	return visdn_pass_get_stats(&netdevice->visdn_chan);
}

static void vnd_netdev_set_multicast_list(
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(
			&visdn_int_cxc.cxc,
			&netdevice->visdn_chan_e);
	if (!dst)
		return;

	if((netdev->flags & IFF_PROMISC) &&
	   !test_bit(VISDN_CHAN_STATE_OPEN, &dst->state)) {

		visdn_open(dst);

	} else if(!(netdev->flags & IFF_PROMISC) &&
	          test_bit(VISDN_CHAN_STATE_OPEN, &dst->state)) {

		visdn_close(dst);
	}

	visdn_chan_put(dst);
}

static int vnd_netdev_do_ioctl(
	struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
//	struct vnd_netdevice *netdevice = netdev->priv;

	return -EOPNOTSUPP;
}

static int vnd_netdev_change_mtu(
	struct net_device *netdev,
	int new_mtu)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	// LOCKING*********** FIXME
	if (new_mtu > netdevice->visdn_chan.pars.mtu)
		return -EINVAL;

	netdev->mtu = new_mtu;

	return 0;
}

struct vnd_request
{
	struct semaphore sem;

	int type;
	char output[100];
	int output_len;
};

static int vnd_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	struct vnd_request *vnd_request;
	vnd_request = kmalloc(sizeof(*vnd_request), GFP_KERNEL);
	if (!vnd_request)
		return -EFAULT;

	memset(vnd_request, 0x00, sizeof(*vnd_request));

	init_MUTEX(&vnd_request->sem);

	file->private_data = vnd_request;

	return 0;
}

static int vnd_cdev_release(
	struct inode *inode, struct file *file)
{
	struct vnd_request *vnd_request = file->private_data;

	kfree(vnd_request);

	return 0;
}

static ssize_t vnd_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err;

	struct vnd_request *vnd_request = file->private_data;

	if (down_interruptible(&vnd_request->sem))
		return -ERESTARTSYS;

	int copied_bytes = 0;

	if (*offp < vnd_request->output_len) {
		copied_bytes = vnd_request->output_len - *offp;
		if (copied_bytes > count)
			copied_bytes = count;

		if (copy_to_user(buf,
				vnd_request->output + *offp,
				copied_bytes)) {
			err = -EFAULT;
			goto err_copy_to_user;
		}

		*offp += copied_bytes;
	}

	up(&vnd_request->sem);

	return copied_bytes;

err_copy_to_user:

	up(&vnd_request->sem);

	return err;
}

static int vnd_create_request(
	struct vnd_request *vnd_request,
	char *reqp)
{
	int err;

	const char *protocol = strsep(&reqp, " \n\r");
	if (!protocol) {
		err = -EINVAL;
		goto err_missing_protocol;
	}

	void (*setup_func)(struct net_device *) = NULL;
	int type;

	if (!strcmp(protocol, "lapd")) {
		type = ARPHRD_LAPD;
		setup_func = setup_lapd;
//	} else if (!strcmp(protocol, "mtp")) {
	} else {
		err = -EINVAL;
		goto err_unsupported_protocol;
	}

	struct vnd_netdevice *netdevice = NULL;
	netdevice = kmalloc(sizeof(*netdevice), GFP_KERNEL);
	if (!netdevice) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(netdevice, 0x00, sizeof(*netdevice));

	atomic_set(&netdevice->refcnt, 1);

	netdevice->index = vnd_netdevice_new_index();
	hlist_add_head(&netdevice->index_hlist_node,
			vnd_netdevice_index_get_hash(netdevice->index));

	// Main chan
	visdn_chan_init(&netdevice->visdn_chan);

	netdevice->visdn_chan.ops = &vnd_chan_ops;
	netdevice->visdn_chan.port = &vnd_port;
	netdevice->visdn_chan.cxc = &visdn_int_cxc.cxc;
	netdevice->visdn_chan.name[0] = '\0';
	netdevice->visdn_chan.driver_data = netdevice;
	netdevice->visdn_chan.autoopen = FALSE;
	netdevice->visdn_chan.max_mtu = 0;
	netdevice->visdn_chan.bitrate_selection =
					 VISDN_CHAN_BITRATE_SELECTION_MAX;
	netdevice->visdn_chan.bitrates_cnt = 0;
	netdevice->visdn_chan.framing_supported =
					VISDN_CHAN_FRAMING_HDLC |
					VISDN_CHAN_FRAMING_MTP;
	netdevice->visdn_chan.framing_preferred = 0;
	netdevice->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB;
	netdevice->visdn_chan.bitorder_preferred = 0;

	// E-chan
	visdn_chan_init(&netdevice->visdn_chan_e);

	netdevice->visdn_chan_e.ops = &vnd_chan_ops;
	netdevice->visdn_chan_e.port = &vnd_port;
	netdevice->visdn_chan_e.cxc = &visdn_int_cxc.cxc;
	netdevice->visdn_chan_e.name[0] = '\0';
	netdevice->visdn_chan_e.driver_data = netdevice;
	netdevice->visdn_chan_e.autoopen = FALSE;
	netdevice->visdn_chan_e.max_mtu = 0;
	netdevice->visdn_chan_e.bitrate_selection =
					 VISDN_CHAN_BITRATE_SELECTION_MAX;
	netdevice->visdn_chan_e.bitrates_cnt = 0;
	netdevice->visdn_chan_e.framing_supported = VISDN_CHAN_FRAMING_HDLC |
					     VISDN_CHAN_FRAMING_MTP;
	netdevice->visdn_chan_e.framing_preferred = 0;
	netdevice->visdn_chan_e.bitorder_supported = VISDN_CHAN_BITORDER_LSB;
	netdevice->visdn_chan_e.bitorder_preferred = 0;

	vnd_netdevice_get(netdevice); // Reference in visdn_chan->driver_data
	err = visdn_chan_register(&netdevice->visdn_chan);
	if (err < 0)
		goto err_visdn_chan_register;

	vnd_netdevice_get(netdevice); // Reference in visdn_chan->driver_data
	err = visdn_chan_register(&netdevice->visdn_chan_e);
	if (err < 0)
		goto err_visdn_chan_e_register;

	char ifname[60];
	snprintf(ifname, sizeof(ifname), "visdn%dd", netdevice->index);

	netdevice->type = type;
	netdevice->netdev = alloc_netdev(0, ifname, setup_func);
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
	netdevice->netdev->change_mtu = vnd_netdev_change_mtu;
	netdevice->netdev->features = NETIF_F_NO_CSUM;

	netdevice->netdev->mtu = netdevice->visdn_chan.pars.mtu;

	memset(netdevice->netdev->dev_addr, 0x00,
		sizeof(netdevice->netdev->dev_addr));

	SET_MODULE_OWNER(netdevice->netdev);

	netdevice->netdev->irq = 0;
	netdevice->netdev->base_addr = 0;

	vnd_request->output_len =
		snprintf(vnd_request->output,
			sizeof(vnd_request->output),
			"%s\n%s\n%s\n",
			netdevice->netdev->name,
			netdevice->visdn_chan.cxc_id,
			netdevice->visdn_chan_e.cxc_id);

	vnd_request->type = type;

	return 0;

	visdn_chan_unregister(&netdevice->visdn_chan_e);
err_visdn_chan_e_register:
	visdn_chan_unregister(&netdevice->visdn_chan);
err_visdn_chan_register:
	free_netdev(netdevice->netdev);
err_alloc_netdev:
	kfree(netdevice);
//	hlist_del(); FIXME
err_kmalloc:
err_unsupported_protocol:
err_missing_protocol:

	return err;
}

static int vnd_destroy_request(
	struct vnd_request *vnd_request,
	char *reqp)
{
	return -ENOTSUPP;
}

static ssize_t vnd_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err;
	char request[50];

	struct vnd_request *vnd_request = file->private_data;

	if (down_interruptible(&vnd_request->sem))
		return -ERESTARTSYS;

	if (copy_from_user(request, buf, min(count, (sizeof(request) - 1)))) {
                err = -EFAULT;
                goto err_copy_from_user;
        }

	request[sizeof(request) - 1] = '\0';

	char *reqp = request;

	const char *command = strsep(&reqp, " ");
	if (!command) {
		err = -EINVAL;
		goto err_missing_command;
	}

	if (!strcmp(command, "create")) {
		err = vnd_create_request(vnd_request, reqp);
		if (err < 0)
			goto err_command;
	} else if (!strcmp(command, "destroy")) {
		err = vnd_destroy_request(vnd_request, reqp);
		if (err < 0)
			goto err_command;
	} else {
		err = -EINVAL;
		goto err_invalid_command;
	}

	*offp = 0;

	up(&vnd_request->sem);

	return count;

err_command:
err_invalid_command:
err_missing_command:
err_copy_from_user:

	up(&vnd_request->sem);

	return err;
}

static struct file_operations vnd_fops =
{
	.owner		= THIS_MODULE,
	.read		= vnd_cdev_read,
	.write		= vnd_cdev_write,
	.ioctl		= NULL,
	.open		= vnd_cdev_open,
	.release	= vnd_cdev_release,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops vnd_port_ops = {
	.owner		= THIS_MODULE,
	.enable		= NULL,
	.disable	= NULL,
};

#ifndef HAVE_CLASS_DEV_DEVT
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

	visdn_port_init(&vnd_port);
	vnd_port.ops = &vnd_port_ops;
	vnd_port.device = &visdn_system_device;
	strncpy(vnd_port.name, "netdev", sizeof(vnd_port.name));;
	err = visdn_port_register(&vnd_port);
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
	vnd_control_class_dev.dev = vnd_port.device;
#ifdef HAVE_CLASS_DEV_DEVT
	vnd_control_class_dev.devt = vnd_first_dev;
#endif
	snprintf(vnd_control_class_dev.class_id,
		sizeof(vnd_control_class_dev.class_id),
		"netdev-control");

	err = class_device_register(&vnd_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
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

			vnd_netdevice_put(netdevice);
		}
	}

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
        	&vnd_control_class_dev,
	        &class_device_attr_dev);
#endif

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

/*
 * vISDN gateway between vISDN's crossconnector and Linux's ppp subsystem
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
#include <linux/device.h>
#include <linux/list.h>

#include <linux/if.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>

#include <visdn.h>
#include <cxc.h>
#include <cxc_internal.h>

#include "ppp.h"

static dev_t vppp_first_dev;
static struct cdev vppp_cdev;
static struct class_device vppp_control_class_dev;

struct hlist_head vppp_chan_index_hash[SB_CHAN_HASHSIZE];

static inline struct hlist_head *vppp_chan_index_get_hash(int index)
{
	return &vppp_chan_index_hash[index & (SB_CHAN_HASHSIZE - 1)];
}

struct vppp_chan *__vppp_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct vppp_chan *vppp_chan;

	hlist_for_each_entry(vppp_chan, t, vppp_chan_index_get_hash(index),
			index_hlist_node) {
		if (vppp_chan->index == index)
			return vppp_chan;
	}

	return NULL;
}

static int vppp_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__vppp_chan_get_by_index(index))
			return index;
	}
}

static struct visdn_port vppp_port;

static void vppp_chan_release(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vppp_chan_release()\n");

	kfree(to_vppp_chan(visdn_chan));
}

static int vppp_chan_open(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vppp_open()\n");

	return 0;
}

static int vppp_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vppp_close()\n");

	return 0;
}

static int vppp_chan_frame_xmit(struct visdn_chan *visdn_chan, struct sk_buff *skb)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_chan);

	printk(KERN_INFO "vppp_chan_frame_xmit()\n");

	ppp_input(&chan->ppp_chan, skb);

	return 0;
}

static void vppp_chan_frame_input_error(struct visdn_chan *visdn_chan, int code)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_chan);

	printk(KERN_INFO "vppp_chan_frame_input_error()\n");

	ppp_input_error(&chan->ppp_chan, code);
}

static int vppp_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	printk(KERN_INFO "PPP gateway channel %s connected to %s\n",
		visdn_chan->cxc_id,
		visdn_chan2->cxc_id);

	return 0;
}

static int vppp_chan_disconnect(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "PPP gateway %s disconnected\n",
		visdn_chan->cxc_id);

	return 0;
}

static int vppp_chan_update_parameters(
        struct visdn_chan *visdn_chan,
        struct visdn_chan_pars *pars)
{
	//struct vppp_chan *chan = to_vppp_chan(visdn_chan);

	if (pars->mtu != visdn_chan->pars.mtu) {
		// We must set visdn_chan->pars.mtu before calling dev_set_mtu
		// because mtu_changed() callback will check for it

		visdn_chan->pars.mtu = pars->mtu;

		//dev_set_mtu(netdevice->netdev, pars->mtu);
	}

        memcpy(&visdn_chan->pars, pars, sizeof(visdn_chan->pars));

        return 0;
}

static void vppp_chan_wake_queue(struct visdn_chan *visdn_chan)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_chan);

	printk(KERN_INFO "vppp_chan_wake_queue()\n");

	ppp_output_wakeup(&chan->ppp_chan);
}

static struct visdn_chan_ops vppp_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= vppp_chan_release,
	.open			= vppp_chan_open,
	.close			= vppp_chan_close,
	.frame_xmit		= vppp_chan_frame_xmit,
	.frame_input_error	= vppp_chan_frame_input_error,
	.get_stats		= NULL,

	.connect_to		= vppp_chan_connect_to,
	.disconnect		= vppp_chan_disconnect,

	.update_parameters	= vppp_chan_update_parameters,

	.samples_read		= NULL,
	.samples_write		= NULL,

	.stop_queue		= NULL,
	.start_queue		= NULL,
	.wake_queue		= vppp_chan_wake_queue,
};

static int vppp_ppp_start_xmit(
	struct ppp_channel *ppp_chan,
	struct sk_buff *skb)
{
	struct vppp_chan *chan = container_of(ppp_chan, struct vppp_chan, ppp_chan);

	printk(KERN_INFO "vppp_ppp_start_xmit()\n");

	return visdn_pass_frame_xmit(&chan->visdn_chan, skb);

	return -ENOTSUPP;
}

static int vppp_ppp_ioctl(
	struct ppp_channel *ppp_chan,
	unsigned int cmd,
	unsigned long arg)
{
	printk(KERN_INFO "vppp_ppp_ioctl()\n");

	return -EINVAL;
}

static struct ppp_channel_ops vppp_ppp_ops =
{
	start_xmit:	vppp_ppp_start_xmit,
	ioctl:		vppp_ppp_ioctl,
};

int vppp_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;

	printk(KERN_INFO "vppp_cdev_open()\n");

	nonseekable_open(inode, file);

	struct vppp_chan *chan;
	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	visdn_chan_init(&chan->visdn_chan);

	chan->index = vppp_chan_new_index();
	chan->visdn_chan.ops = &vppp_chan_ops;
	chan->visdn_chan.port = &vppp_port;
	chan->visdn_chan.cxc = &visdn_int_cxc.cxc;
	chan->visdn_chan.name[0] = '\0';
	chan->visdn_chan.driver_data = chan;
	chan->visdn_chan.autoopen = FALSE;
	chan->visdn_chan.max_mtu = 200; // FIXME
	chan->visdn_chan.bitrate_selection = VISDN_CHAN_BITRATE_SELECTION_MAX;
	chan->visdn_chan.bitrates_cnt = 0;
	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_HDLC;
	chan->visdn_chan.framing_preferred = 0;
	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB;
	chan->visdn_chan.bitorder_preferred = 0;

	chan->ppp_chan.private = chan;
	chan->ppp_chan.ops = &vppp_ppp_ops;
	chan->ppp_chan.mtu = 200; //FIXME
	chan->ppp_chan.hdrlen = 2;

	err = ppp_register_channel(&chan->ppp_chan);
	if (err < 0)
		goto err_ppp_register_channel;

	char chanid[10];
	snprintf(chanid, sizeof(chanid), "%d", chan->index);

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	file->private_data = chan;

	printk(KERN_WARNING "ppp channel %d opened\n", chan->index);

	return 0;

	visdn_chan_unregister(&chan->visdn_chan);
err_chan_register:
	ppp_unregister_channel(&chan->ppp_chan);
err_ppp_register_channel:
	kfree(chan);
err_kmalloc:

	return err;
}

int vppp_cdev_release(
	struct inode *inode, struct file *file)
{
	BUG_ON(!file->private_data);

	printk(KERN_INFO "vppp_cdev_release()\n");

	struct vppp_chan *chan = file->private_data;

	visdn_pass_close(&chan->visdn_chan);

	ppp_unregister_channel(&chan->ppp_chan);
	visdn_chan_unregister(&chan->visdn_chan);

	return 0;
}

ssize_t vppp_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

//	struct vppp_chan *chan = file->private_data;
	printk(KERN_INFO "vppp_cdev_read()\n");

	return -ENOTSUPP;
}

ssize_t vppp_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

//	struct vppp_chan *chan = file->private_data;
	printk(KERN_INFO "vppp_cdev_write()\n");

	return -ENOTSUPP;
}

static inline int visdn_cdev_do_ioctl_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;

	struct vppp_chan *chan = file->private_data;

	struct visdn_connect connect;

 	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	printk(KERN_DEBUG "ioctl(IOC_CONNECT, '%s', '%s')\n",
		connect.src_chanid,
		connect.dst_chanid);

	struct visdn_chan *visdn_chan2;
	visdn_chan2 = visdn_cxc_search_chan(&visdn_int_cxc.cxc,
				connect.dst_chanid);
	if (!visdn_chan2) {
		err = -ENODEV;
		goto err_search_dst;
	}

	if (visdn_chan2 == &chan->visdn_chan) {
		err = -EINVAL;
		goto err_connect_self;
	}

	err = visdn_connect(&chan->visdn_chan, visdn_chan2, connect.flags);
	if (err < 0)
		goto err_connect;

	err = visdn_pass_open(&chan->visdn_chan);
	if (err < 0)
		goto err_open;

	// Release reference returned by visdn_search_chan()
	visdn_chan_put(visdn_chan2);

	return 0;

	visdn_pass_close(&chan->visdn_chan);
err_open:
	visdn_disconnect(&chan->visdn_chan);
err_connect:
err_connect_self:
	visdn_chan_put(visdn_chan2);
err_search_dst:
err_copy_from_user:

	return err;
}

int vppp_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vppp_chan *chan = file->private_data;

	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return visdn_cdev_do_ioctl_connect(inode, file, cmd, arg);
	break;
	case PPPIOCGCHAN: {
		int __user *buf = (int __user *)arg;

		if (put_user(ppp_channel_index(&chan->ppp_chan), buf))
			return -EFAULT;
	}
	break;

	case PPPIOCGUNIT: {
		int __user *buf = (int __user *)arg;

		if (put_user(ppp_unit_number(&chan->ppp_chan), buf))
			return -EFAULT;
	}
	break;

	default:
		printk(KERN_ERR "IOCTL(%d,%ld)\n", cmd, arg);
		return -EINVAL;
	}

	return 0;
}

struct file_operations vppp_fops =
{
	.owner		= THIS_MODULE,
	.read		= vppp_cdev_read,
	.write		= vppp_cdev_write,
	.ioctl		= vppp_cdev_ioctl,
	.open		= vppp_cdev_open,
	.release	= vppp_cdev_release,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops vppp_port_ops = {
	.owner		= THIS_MODULE,
	.enable		= NULL,
	.disable	= NULL,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vppp_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vppp_init_module(void)
{
	int err;

	printk(KERN_INFO vppp_MODULE_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(vppp_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&vppp_chan_index_hash[i]);
	}

	visdn_port_init(&vppp_port);
	vppp_port.ops = &vppp_port_ops;
	vppp_port.device = &visdn_system_device;
	strncpy(vppp_port.name, "ppp", sizeof(vppp_port.name));
	err = visdn_port_register(&vppp_port);
	if (err < 0)
		goto err_visdn_port_register;

	err = alloc_chrdev_region(&vppp_first_dev, 0, 1, vppp_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vppp_cdev, &vppp_fops);
	vppp_cdev.owner = THIS_MODULE;

	err = cdev_add(&vppp_cdev, vppp_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	vppp_control_class_dev.class = &visdn_system_class;
	vppp_control_class_dev.class_data = NULL;
	vppp_control_class_dev.dev = vppp_port.device;
#ifdef HAVE_CLASS_DEV_DEVT
	vppp_control_class_dev.devt = vppp_first_dev;
#endif
	snprintf(vppp_control_class_dev.class_id,
		sizeof(vppp_control_class_dev.class_id),
		"ppp");

	err = class_device_register(&vppp_control_class_dev);
	if (err < 0)
		goto err_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&vppp_control_class_dev,
		&class_device_attr_dev);
#endif

	return 0;

	class_device_unregister(&vppp_control_class_dev);
err_class_device_register:
	visdn_port_unregister(&vppp_port);
err_visdn_port_register:
	cdev_del(&vppp_cdev);
err_cdev_add:
	unregister_chrdev_region(vppp_first_dev, 1);
err_register_chrdev:

	return err;
}

module_init(vppp_init_module);

static void __exit vppp_module_exit(void)
{

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vppp_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&vppp_control_class_dev);
	visdn_port_unregister(&vppp_port);
	cdev_del(&vppp_cdev);
	unregister_chrdev_region(vppp_first_dev, 1);

	printk(KERN_INFO vppp_MODULE_DESCR " unloaded\n");
}

module_exit(vppp_module_exit);

MODULE_DESCRIPTION(vppp_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

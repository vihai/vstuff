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

#include <linux/visdn/core.h>
#include <linux/visdn/port.h>
#include <linux/visdn/softcxc.h>

#include "ppp.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static dev_t vppp_first_dev;
static struct cdev vppp_cdev;
static struct class_device vppp_control_class_dev;

static struct visdn_port vppp_port;

static void vppp_chan_release(struct visdn_chan *visdn_chan)
{
	vppp_debug(3, "vppp_chan_release()\n");

	kfree(to_vppp_chan(visdn_chan));
}

static int vppp_chan_open(struct visdn_chan *visdn_chan)
{
	vppp_debug(3, "vppp_open()\n");

	return 0;
}

static int vppp_chan_close(struct visdn_chan *visdn_chan)
{
	vppp_debug(3, "vppp_close()\n");

	return 0;
}

static int vppp_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_leg->chan);

	vppp_debug(3, "vppp_chan_frame_xmit()\n");

	ppp_input(&chan->ppp_chan, skb);

	return 0;
}

static int vppp_chan_connect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vppp_debug(2, "PPP gateway channel %06d connected to %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	return 0;
}

static void vppp_chan_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vppp_debug(2, "PPP gateway %06d disconnected from %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);
}

static void vppp_chan_wake_queue(
	struct visdn_leg *visdn_leg)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_leg->chan);

	vppp_debug(3, "vppp_chan_wake_queue()\n");

	ppp_output_wakeup(&chan->ppp_chan);
}

static void vppp_chan_rx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_rx_error_code code)
{
	struct vppp_chan *chan = to_vppp_chan(visdn_leg->chan);

	ppp_input_error(&chan->ppp_chan, 0);
}

static void vppp_chan_tx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_tx_error_code code)
{
}

static struct visdn_chan_ops vppp_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= vppp_chan_release,
	.open			= vppp_chan_open,
	.close			= vppp_chan_close,
};

static struct visdn_leg_ops vppp_leg_ops = {
	.owner			= THIS_MODULE,

	.connect		= vppp_chan_connect,
	.disconnect		= vppp_chan_disconnect,

	.frame_xmit		= vppp_chan_frame_xmit,

	.stop_queue		= NULL,
	.start_queue		= NULL,
	.wake_queue		= vppp_chan_wake_queue,

	.rx_error		= vppp_chan_rx_error,
	.tx_error		= vppp_chan_tx_error,
};

static int vppp_ppp_start_xmit(
	struct ppp_channel *ppp_chan,
	struct sk_buff *skb)
{
	struct vppp_chan *chan =
		container_of(ppp_chan, struct vppp_chan, ppp_chan);
	int res;

	vppp_debug(3, "vppp_ppp_start_xmit()\n");

	res = visdn_leg_frame_xmit(&chan->visdn_chan.leg_a, skb);
	switch(res) {
	case VISDN_TX_OK:
		return 1;
	case VISDN_TX_BUSY:
	case VISDN_TX_LOCKED:
		schedule_work(&chan->retry_work);
		return 0;
	default:
		return 0;
	}
}

static void vppp_retry_work(void *data)
{
	struct vppp_chan *chan = data;

	ppp_output_wakeup(&chan->ppp_chan);
}

static int vppp_ppp_ioctl(
	struct ppp_channel *ppp_chan,
	unsigned int cmd,
	unsigned long arg)
{
	vppp_debug(3, "vppp_ppp_ioctl()\n");

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
	struct vppp_chan *chan;
	int err;

	vppp_debug(3, "vppp_cdev_open()\n");

	nonseekable_open(inode, file);

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	INIT_WORK(&chan->retry_work,
		vppp_retry_work,
		chan);

	visdn_chan_init(&chan->visdn_chan);

	chan->visdn_chan.ops = &vppp_chan_ops;
	chan->visdn_chan.chan_class = NULL;
	chan->visdn_chan.port = &vppp_port;

	chan->visdn_chan.leg_a.ops = &vppp_leg_ops;
	chan->visdn_chan.leg_a.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.ops = NULL;
	chan->visdn_chan.leg_b.cxc = NULL;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_b.mtu = -1;

	chan->visdn_chan.driver_data = chan;

	chan->ppp_chan.private = chan;
	chan->ppp_chan.ops = &vppp_ppp_ops;
	chan->ppp_chan.mtu = 200; //FIXME
	chan->ppp_chan.hdrlen = 2;

	err = ppp_register_channel(&chan->ppp_chan);
	if (err < 0)
		goto err_ppp_register_channel;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	file->private_data = chan;

	vppp_debug(2, "ppp channel %06d opened\n", chan->visdn_chan.id);

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
	struct vppp_chan *chan = file->private_data;

	BUG_ON(!file->private_data);

	vppp_debug(3, "vppp_cdev_release()\n");

	// Disable channels

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
	vppp_debug(3, "vppp_cdev_read()\n");

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
	vppp_debug(3, "vppp_cdev_write()\n");

	return -ENOTSUPP;
}

int vppp_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vppp_chan *chan = file->private_data;

	switch(cmd) {
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
		vppp_debug(2, "IOCTL(%d,%ld)\n", cmd, arg);
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

#ifdef DEBUG_CODE
static ssize_t vppp_show_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vppp_store_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vppp_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

VISDN_PORT_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vppp_show_debug_level,
	vppp_store_debug_level);
#endif

static int __init vppp_init_module(void)
{
	int err;

	vppp_msg(KERN_INFO, vppp_MODULE_DESCR " loading\n");

	visdn_port_init(&vppp_port);
	vppp_port.ops = &vppp_port_ops;
	vppp_port.device = &visdn_system_device;
	strncpy(vppp_port.name, "ppp", sizeof(vppp_port.name));

	err = visdn_port_register(&vppp_port);
	if (err < 0)
		goto err_visdn_port_register;

#ifdef DEBUG_CODE
	err = visdn_port_create_file(
		&vppp_port,
		&visdn_port_attr_debug_level);
	if (err < 0)
		goto err_create_file_debug_level;
#endif

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
	cdev_del(&vppp_cdev);
err_cdev_add:
	unregister_chrdev_region(vppp_first_dev, 1);
err_register_chrdev:
#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vppp_port,
		&visdn_port_attr_debug_level);
err_create_file_debug_level:
#endif
	visdn_port_unregister(&vppp_port);
err_visdn_port_register:

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
	cdev_del(&vppp_cdev);
	unregister_chrdev_region(vppp_first_dev, 1);

#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vppp_port,
		&visdn_port_attr_debug_level);
#endif

	visdn_port_unregister(&vppp_port);

	vppp_msg(KERN_INFO, vppp_MODULE_DESCR " unloaded\n");
}

module_exit(vppp_module_exit);

MODULE_DESCRIPTION(vppp_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

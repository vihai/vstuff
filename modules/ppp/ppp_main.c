/*
 * vISDN gateway between vISDN's crossconnector and Linux's ppp subsystem
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
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

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/softswitch.h>

#include "ppp.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

static dev_t vppp_first_dev;
static struct cdev vppp_cdev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static struct class_device vppp_control_device;
#else
static struct device vppp_control_device;
#endif

static u8 ppphdr[] = { 0xff, 0x03 };

static int vppp_ppp_start_xmit(
	struct ppp_channel *ppp_chan,
	struct sk_buff *skb)
{
	struct vppp_chan *chan =
		container_of(ppp_chan, struct vppp_chan, ppp_chan);
	int res;

	vppp_debug(3, "vppp_ppp_start_xmit()\n");

	if (test_bit(VPPP_CHAN_STATUS_QUEUE_STOPPED, &chan->status))
		return 0;

	memcpy(skb_push(skb, sizeof(ppphdr)), ppphdr, sizeof(ppphdr));

	/* Put in a fake CRC */
	memset(skb_put(skb, sizeof(u16)), 0, sizeof(u16));

	res = kss_chan_push_frame(&chan->ks_chan_tx, skb);
	switch(res) {
	case KSS_TX_OK:
		return 1;
	case KSS_TX_FULL:
		set_bit(VPPP_CHAN_STATUS_QUEUE_STOPPED, &chan->status);
	case KSS_TX_BUSY:
	case KSS_TX_LOCKED:
		tasklet_schedule(&chan->wakeup_tasklet);
		return 0;
	default:
		return 0;
	}
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

static void vppp_rx_tasklet(unsigned long data)
{
	struct vppp_chan *chan = (struct vppp_chan *)data;
	unsigned long flags;
	struct sk_buff *skb;

	for(;;) {
		spin_lock_irqsave(&chan->rx_queue_lock, flags);
		skb = skb_dequeue(&chan->rx_queue);
		spin_unlock_irqrestore(&chan->rx_queue_lock, flags);

		if (!skb)
			return;

		ppp_input(&chan->ppp_chan, skb);
	}
}

static void vppp_rx_error_tasklet(unsigned long data)
{
	struct vppp_chan *chan = (struct vppp_chan *)data;

	ppp_input_error(&chan->ppp_chan, 0);
}

static void vppp_chan_release(
	struct kref *kref)
{
	struct vppp_chan *chan =
		container_of(kref, struct vppp_chan, kref);

	vppp_debug(3, "vppp_chan_put(): releasing\n");

	kfree(chan);
}

static struct vppp_chan *vppp_chan_get(
	struct vppp_chan *chan)
{
	if (chan) {
		WARN_ON(atomic_read(&chan->kref.refcount) <= 0);

		kref_get(&chan->kref);
	}

	return chan;
}

static void vppp_chan_put(
	struct vppp_chan *chan)
{
	if (chan)
		WARN_ON(atomic_read(&chan->kref.refcount) <= 0);

	kref_put(&chan->kref, vppp_chan_release);
}




/*---------------------------------------------------------------------------*/

static void vppp_chan_rx_release(struct ks_chan *ks_chan)
{
	struct vppp_chan *chan =
		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	vppp_debug_chan(chan, 3, "vppp_chan_rx_release()\n");

	vppp_chan_put(chan);
}

static int vppp_chan_rx_open(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	vppp_debug_chan(chan, 3, "vppp_chan_rx_open()\n");

	return 0;
}

static void vppp_chan_rx_close(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	vppp_debug_chan(chan, 3, "vppp_chan_rx_close()\n");
}

static int vppp_chan_rx_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct vppp_chan *chan =
		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	/* Throw away address and control bytes */
	skb_pull(skb, 2);

	if (irqs_disabled()) {
		unsigned long flags;

		spin_lock_irqsave(&chan->rx_queue_lock, flags);
		skb_queue_tail(&chan->rx_queue, skb);
		spin_unlock_irqrestore(&chan->rx_queue_lock, flags);

		tasklet_schedule(&chan->rx_tasklet);
	} else {
		ppp_input(&chan->ppp_chan, skb);
	}

	return KSS_TX_OK;
}

static int vppp_chan_rx_connect(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	vppp_debug_chan(chan, 2, "RX connected\n");


	return 0;
}

static void vppp_chan_rx_disconnect(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_rx);

	vppp_debug_chan(chan, 2, "disconnected\n");
}

static struct ks_chan_ops vppp_chan_rx_ops = {
	.owner			= THIS_MODULE,
	.release	  	= vppp_chan_rx_release,
	.connect	  	= vppp_chan_rx_connect,
	.disconnect		= vppp_chan_rx_disconnect,
	.open		  	= vppp_chan_rx_open,
	.close			= vppp_chan_rx_close,
};

static struct kss_chan_from_ops vppp_chan_rx_softswitch_ops = {
	.push_frame		= vppp_chan_rx_push_frame,
};

/*---------------------------------------------------------------------------*/

static void vppp_chan_tx_release(struct ks_chan *ks_chan)
{
	struct vppp_chan *chan =
		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

	vppp_debug_chan(chan, 3, "vppp_chan_tx_release()\n");

	vppp_chan_put(chan);
}

static int vppp_chan_tx_open(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

//	struct ks_pipeline *pipeline = chan->ks_chan_tx.pipeline;

	vppp_debug_chan(chan, 3, "vppp_chan_tx_open()\n");

	return 0;
}

static void vppp_chan_tx_close(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

	vppp_debug_chan(chan, 3, "vppp_chan_tx_close()\n");
}

static int vppp_chan_tx_connect(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

	vppp_debug_chan(chan, 2, "vppp_chan_tx_connect()\n");

	return 0;
}

static void vppp_chan_tx_disconnect(struct ks_chan *ks_chan)
{
//	struct vppp_chan *chan =
//		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

	vppp_debug_chan(chan, 2, "vppp_chan_tx_disconnect()\n");
}

static int vppp_chan_tx_start(struct ks_chan *ks_chan)
{

	return 0;
}

static void vppp_chan_tx_stop(struct ks_chan *ks_chan)
{
}

static void vppp_wakeup_tasklet(unsigned long data)
{
	struct vppp_chan *chan = (struct vppp_chan *)data;

	ppp_output_wakeup(&chan->ppp_chan);
}

static void vppp_chan_tx_wake_queue(struct ks_chan *ks_chan)
{
	struct vppp_chan *chan =
		container_of(ks_chan, struct vppp_chan, ks_chan_tx);

	clear_bit(VPPP_CHAN_STATUS_QUEUE_STOPPED, &chan->status);

	if (irqs_disabled())
		tasklet_schedule(&chan->wakeup_tasklet);
	else
		ppp_output_wakeup(&chan->ppp_chan);
}

static struct ks_chan_ops vppp_chan_tx_ops = {
	.owner			= THIS_MODULE,
	.release		= vppp_chan_tx_release,
	.connect		= vppp_chan_tx_connect,
	.disconnect		= vppp_chan_tx_disconnect,
	.open			= vppp_chan_tx_open,
	.close			= vppp_chan_tx_close,
	.start			= vppp_chan_tx_start,
	.stop			= vppp_chan_tx_stop,
};

static struct kss_chan_to_ops vppp_chan_tx_softswitch_ops = {
	.wake_queue		= vppp_chan_tx_wake_queue,
};

/*---------------------------------------------------------------------------*/

static void vppp_node_release(struct ks_node *ks_node)
{
	struct vppp_chan *chan =
		container_of(ks_node, struct vppp_chan, ks_node);

	vppp_debug_chan(chan, 3, "vppp_node_release()\n");

	vppp_chan_put(chan);
}

static struct ks_node_ops vppp_node_ops = {
	.owner			= THIS_MODULE,

	.release		= vppp_node_release,
};

/*---------------------------------------------------------------------------*/

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

	tasklet_init(&chan->wakeup_tasklet,
		vppp_wakeup_tasklet,
		(unsigned long)chan);

	tasklet_init(&chan->rx_tasklet,
		vppp_rx_tasklet,
		(unsigned long)chan);

	tasklet_init(&chan->rx_error_tasklet,
		vppp_rx_error_tasklet,
		(unsigned long)chan);

	skb_queue_head_init(&chan->rx_queue);
	spin_lock_init(&chan->rx_queue_lock);

	ks_node_create(&chan->ks_node,
		&vppp_node_ops, "paperino",
		&ks_system_device.kobj);

	/* RX */
	ks_chan_create(&chan->ks_chan_rx,
			&vppp_chan_rx_ops, "rx",
			NULL,
			&chan->ks_node.kobj,
			&kss_softswitch.ks_node,
			&chan->ks_node);

	chan->ks_chan_rx.from_ops = &vppp_chan_rx_softswitch_ops;

	/* TX */
	ks_chan_create(&chan->ks_chan_tx,
			&vppp_chan_tx_ops, "tx",
			NULL,
			&chan->ks_node.kobj,
			&chan->ks_node,
			&kss_softswitch.ks_node);

	chan->ks_chan_tx.to_ops = &vppp_chan_tx_softswitch_ops;



	err = ks_node_register(&chan->ks_node);
	if (err < 0)
		goto err_node_register;

	err = ks_chan_register(&chan->ks_chan_rx);
	if (err < 0)
		goto err_chan_rx_register;

	err = ks_chan_register(&chan->ks_chan_tx);
	if (err < 0)
		goto err_chan_tx_register;


/*	pipeline = visdn_pipeline_get_by_endpoint(ks_chan);
	if (!pipeline)
		return -ENOTCONN;*/

	chan->ppp_chan.private = chan;
	chan->ppp_chan.ops = &vppp_ppp_ops;
	chan->ppp_chan.mtu = 300; //visdn_pipeline_find_lowest_mtu(pipeline);
	chan->ppp_chan.hdrlen = sizeof(ppphdr) + sizeof(u16);

//	visdn_pipeline_put(pipeline);

	err = ppp_register_channel(&chan->ppp_chan);
	if (err < 0)
		goto err_ppp_register_channel;

	file->private_data = chan;

	vppp_debug(2, "ppp channel %06d opened\n", chan->ks_node.id);

	return 0;

	ppp_unregister_channel(&chan->ppp_chan);
err_ppp_register_channel:
	ks_chan_unregister(&chan->ks_chan_tx);
err_chan_tx_register:
	ks_chan_unregister(&chan->ks_chan_rx);
err_chan_rx_register:
	ks_node_unregister(&chan->ks_node);
err_node_register:
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

	ppp_unregister_channel(&chan->ppp_chan);

	ks_chan_unregister(&chan->ks_chan_tx);
	ks_chan_unregister(&chan->ks_chan_rx);
	ks_node_unregister(&chan->ks_node);

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

	case VISDN_PPP_GET_CHANID:
		return put_user(chan->ks_node.id, (unsigned int *)arg);
	break;

	default:
		vppp_msg(KERN_WARNING, "Unsupported ioctl(%d, %ld)\n",
			cmd, arg);

		return -EOPNOTSUPP;
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

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static ssize_t show_dev(struct class_device *class_dev, char *buf)
#else
static ssize_t show_dev(struct device *dev, char *buf)
#endif
{
	return print_dev_t(buf, vppp_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vppp_init_module(void)
{
	int err;

	vppp_msg(KERN_INFO, vppp_MODULE_DESCR " loading\n");

	err = alloc_chrdev_region(&vppp_first_dev, 0, 1, vppp_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vppp_cdev, &vppp_fops);
	vppp_cdev.owner = THIS_MODULE;

	err = cdev_add(&vppp_cdev, vppp_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	vppp_control_device.class = &ks_system_class;

#if   LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	vppp_control_device.class_data = NULL;
	vppp_control_device.dev = &ks_system_device;
	snprintf(vppp_control_device.class_id,
		sizeof(vppp_control_device.class_id),
		"ppp");
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30) 
	snprintf(vppp_control_device.bus_id,
		sizeof(vppp_control_device.bus_id),
		"ppp");
#else
	dev_set_name(&vppp_control_device,"ppp");
#endif
	
#ifdef HAVE_CLASS_DEV_DEVT
	vppp_control_device.devt = vppp_first_dev;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	err = class_device_register(&vppp_control_device);
	if (err < 0)
		goto err_device_register;
#else
	err = device_register(&vppp_control_device);
	if (err < 0)
		goto err_device_register;
#endif

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_create_file(
		&vppp_control_device,
		&class_device_attr_dev);
#else
	device_create_file(
		&vppp_control_device,
		&device_attr_dev);
#endif
#endif

	return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_unregister(&vppp_control_device);
#else
	device_unregister(&vppp_control_device);
#endif
err_device_register:
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_remove_file(
		&vppp_control_device,
		&class_device_attr_dev);
#else
	device_remove_file(
		&vppp_control_device,
		&device_attr_dev);
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_unregister(&vppp_control_device);
#else
	device_unregister(&vppp_control_device);
#endif

	cdev_del(&vppp_cdev);
	unregister_chrdev_region(vppp_first_dev, 1);

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

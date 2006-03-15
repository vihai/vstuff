/*
 * vISDN gateway between vISDN's crossconnector and userland for stream access
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

#include <linux/visdn/softcxc.h>
#include <linux/visdn/leg.h>
#include <linux/visdn/port.h>
#include <linux/visdn/path.h>
#include <linux/visdn/router.h>

#include "streamport.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

static dev_t vsp_first_dev;
static struct cdev vsp_cdev;
static struct class_device vsp_class_dev;

static struct visdn_port vsp_port;

static void vsp_chan_release(struct visdn_chan *visdn_chan)
{
	vsp_debug(3, "vsp_chan_release()\n");
}

static int vsp_chan_open(struct visdn_chan *visdn_chan)
{
	vsp_debug(3, "vsp_chan_open()\n");

	return 0;
}

static int vsp_chan_close(struct visdn_chan *visdn_chan)
{
	vsp_debug(3, "vsp_chan_close()\n");

	return 0;
}

static ssize_t vsp_chan_read(
	struct visdn_leg *visdn_leg,
	void *buf, size_t count)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_leg->chan);

	return __kfifo_get(chan->tx_fifo, buf, count);
}

static ssize_t vsp_chan_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)

{
	struct vsp_chan *chan = to_vsp_chan(visdn_leg->chan);

	return __kfifo_put(chan->rx_fifo, (void *)buf, count);
}

static void vsp_chan_rx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_rx_error_code code)
{
}

static void vsp_chan_tx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_tx_error_code code)
{
}

static int vsp_chan_connect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vsp_debug(2, "Streamport %06d connected to %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	return 0;
}

static void vsp_chan_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vsp_debug(2, "Streamport %06d disconnected from %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops vsp_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vsp_chan_release,
	.open		= vsp_chan_open,
	.close		= vsp_chan_close,
};

static struct visdn_leg_ops vsp_leg_ops = {
	.owner		= THIS_MODULE,

	.connect	= vsp_chan_connect,
	.disconnect	= vsp_chan_disconnect,

	.read		= vsp_chan_read,
	.write		= vsp_chan_write,

	.rx_error	= vsp_chan_rx_error,
	.tx_error	= vsp_chan_tx_error,
};

/*---------------------------------------------------------------------------*/

static ssize_t vsp_show_rx_fifo_usage(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->rx_fifo));
}

static ssize_t vsp_store_rx_fifo_usage(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	kfifo_reset(chan->rx_fifo);

	return count;
}

static VISDN_CHAN_ATTR(rx_fifo_usage, S_IRUGO | S_IWUSR,
		vsp_show_rx_fifo_usage,
		vsp_store_rx_fifo_usage);

/*---------------------------------------------------------------------------*/

static ssize_t vsp_show_tx_fifo_usage(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->tx_fifo));
}

static ssize_t vsp_store_tx_fifo_usage(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	kfifo_reset(chan->tx_fifo);

	return count;
}

static VISDN_CHAN_ATTR(tx_fifo_usage, S_IRUGO | S_IWUSR,
		vsp_show_tx_fifo_usage,
		vsp_store_tx_fifo_usage);

/*---------------------------------------------------------------------------*/

static int vsp_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct vsp_chan *chan;

	nonseekable_open(inode, file);

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(chan, 0, sizeof(*chan));

	spin_lock_init(&chan->rx_fifo_lock);
	chan->rx_fifo = kfifo_alloc(1024, GFP_KERNEL, &chan->rx_fifo_lock);
	if (!chan->rx_fifo) {
		err = -EFAULT;
		goto err_fifo_rx_alloc;
	}

	spin_lock_init(&chan->tx_fifo_lock);
	chan->tx_fifo = kfifo_alloc(1024, GFP_KERNEL, &chan->tx_fifo_lock);
	if (!chan->tx_fifo) {
		err = -EFAULT;
		goto err_fifo_tx_alloc;
	}

	visdn_chan_init(&chan->visdn_chan);

	chan->visdn_chan.ops = &vsp_chan_ops;
	chan->visdn_chan.chan_class = NULL;
	chan->visdn_chan.port = &vsp_port;

	chan->visdn_chan.leg_a.ops = &vsp_leg_ops;
	chan->visdn_chan.leg_a.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.ops = NULL;
	chan->visdn_chan.leg_b.cxc = NULL;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.mtu = -1;

	strcpy(chan->visdn_chan.name, "");

	chan->visdn_chan.driver_data = chan;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	file->private_data = chan;

	err = visdn_chan_create_file(&chan->visdn_chan,
					&visdn_chan_attr_rx_fifo_usage);
	if (err < 0)
		goto err_create_file_rx_fifo;

	err = visdn_chan_create_file(&chan->visdn_chan,
					&visdn_chan_attr_tx_fifo_usage);
	if (err < 0)
		goto err_create_file_tx_fifo;

	vsp_debug(2, "Streamport %06d opened\n", chan->visdn_chan.id);

	return 0;

	visdn_chan_remove_file(&chan->visdn_chan,
				&visdn_chan_attr_tx_fifo_usage);
err_create_file_tx_fifo:
	visdn_chan_remove_file(&chan->visdn_chan,
				&visdn_chan_attr_rx_fifo_usage);
err_create_file_rx_fifo:
	visdn_chan_unregister(&chan->visdn_chan);
err_chan_register:
	kfifo_free(chan->tx_fifo);
err_fifo_tx_alloc:
	kfifo_free(chan->rx_fifo);
err_fifo_rx_alloc:
	kfree(chan);
err_kmalloc:

	return err;
}

static int vsp_cdev_release(
	struct inode *inode, struct file *file)
{
	struct vsp_chan *chan = file->private_data;
	struct visdn_path *path;

	vsp_debug(3, "vsp_cdev_release()\n");

	path = visdn_path_get_by_endpoint(&chan->visdn_chan);
	if (path)
		visdn_path_disconnect(path);

	visdn_chan_remove_file(&chan->visdn_chan,
				&visdn_chan_attr_tx_fifo_usage);
	visdn_chan_remove_file(&chan->visdn_chan,
				&visdn_chan_attr_rx_fifo_usage);

	visdn_chan_unregister(&chan->visdn_chan);

	kfifo_free(chan->tx_fifo);
	kfifo_free(chan->rx_fifo);

	/* No references should be left */
	kfree(chan);

	return 0;
}

ssize_t __kfifo_get_user(
	struct kfifo *fifo,
	void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->in - fifo->out));

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, (ssize_t)(fifo->size - (fifo->out & (fifo->size - 1))));
	if (copy_to_user(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l))
		return -EFAULT;

	/* then get the rest (if any) from the beginning of the buffer */
	if (copy_to_user(buffer + l, fifo->buffer, len - l))
		return -EFAULT;

	fifo->out += len;

	return len;
}

static ssize_t vsp_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vsp_chan *chan = file->private_data;
	int err;

	// No locking needed as there is only one reader
	int copied = __kfifo_get_user(chan->rx_fifo, buf, count);
	if (copied < 0) {
		err = -EFAULT;
		goto err_kfifo_get_user;
	}

	return copied;

err_kfifo_get_user:

	return err;
}

ssize_t __kfifo_put_user(
	struct kfifo *fifo,
	const void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->size - fifo->in + fifo->out));

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, (ssize_t)(fifo->size - (fifo->in & (fifo->size - 1))));
	if (copy_from_user(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l))
		return -EFAULT;

	/* then put the rest (if any) at the beginning of the buffer */
	if (copy_from_user(fifo->buffer, buffer + l, len - l))
		return -EFAULT;

	fifo->in += len;

	return len;
}

static ssize_t vsp_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err;
	struct vsp_chan *chan = file->private_data;

	// No locking needed as there is only one reader
	int nwrote = __kfifo_put_user(chan->tx_fifo, buf, count);
	if (nwrote < 0) {
		err = -EFAULT;
		goto err_kfifo_put_user;
	}

	return nwrote;

err_kfifo_put_user:

	return err;
}

static int vsp_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vsp_chan *chan = file->private_data;

	switch(cmd) {
	case VISDN_SP_GET_CHANID:
		return put_user(chan->visdn_chan.id, (unsigned int *)arg);
	break;
	}

	return -EOPNOTSUPP;
}

static struct file_operations vsp_fops =
{
	.owner		= THIS_MODULE,
	.read		= vsp_cdev_read,
	.write		= vsp_cdev_write,
	.ioctl		= vsp_cdev_ioctl,
	.open		= vsp_cdev_open,
	.release	= vsp_cdev_release,
	.llseek		= no_llseek,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vsp_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

#ifdef DEBUG_CODE
static ssize_t vsp_show_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vsp_store_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vsp_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

VISDN_PORT_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vsp_show_debug_level,
	vsp_store_debug_level);
#endif

/******************************************
 * Module stuff
 ******************************************/

static struct visdn_port_ops vsp_port_ops = {
	.owner		= THIS_MODULE,
	.enable		= NULL,
	.disable	= NULL,
};

static int __init vsp_init_module(void)
{
	int err;

	vsp_msg(KERN_INFO, vsp_MODULE_DESCR " loading\n");

	visdn_port_init(&vsp_port);
	vsp_port.ops = &vsp_port_ops;
	vsp_port.device = &visdn_system_device;
	strncpy(vsp_port.name, vsp_MODULE_NAME, sizeof(vsp_port.name));

	err = visdn_port_register(&vsp_port);
	if (err < 0)
		goto err_visdn_port_register;

#ifdef DEBUG_CODE
	err = visdn_port_create_file(
		&vsp_port,
		&visdn_port_attr_debug_level);
	if (err < 0)
		goto err_create_file_debug_level;
#endif

	err = alloc_chrdev_region(&vsp_first_dev, 0, 1, vsp_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vsp_cdev, &vsp_fops);
	vsp_cdev.owner = THIS_MODULE;

	err = cdev_add(&vsp_cdev, vsp_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	snprintf(vsp_class_dev.class_id,
		sizeof(vsp_class_dev.class_id),
		"streamport");
	vsp_class_dev.class = &visdn_system_class;
	vsp_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	vsp_class_dev.devt = vsp_first_dev;
#endif

	err = class_device_register(&vsp_class_dev);
	if (err < 0)
		goto err_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&vsp_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_class_device_create_file;
#endif

	return 0;

	class_device_unregister(&vsp_class_dev);
err_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vsp_class_dev,
		&class_device_attr_dev);
err_class_device_create_file:
#endif
	cdev_del(&vsp_cdev);
err_cdev_add:
	unregister_chrdev_region(vsp_first_dev, 1);
err_register_chrdev:
#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vsp_port,
		&visdn_port_attr_debug_level);
err_create_file_debug_level:
#endif
	visdn_port_unregister(&vsp_port);
err_visdn_port_register:

	return err;
}

module_init(vsp_init_module);

static void __exit vsp_module_exit(void)
{
	// We should free all channels, here!

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vsp_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&vsp_class_dev);

	cdev_del(&vsp_cdev);
	unregister_chrdev_region(vsp_first_dev, 1);

#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vsp_port,
		&visdn_port_attr_debug_level);
#endif

	visdn_port_unregister(&vsp_port);

	vsp_msg(KERN_INFO, vsp_MODULE_DESCR " unloaded\n");
}

module_exit(vsp_module_exit);

MODULE_DESCRIPTION(vsp_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

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

#include <cxc_internal.h>

#include "streamport.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static dev_t vsp_first_dev;
static struct cdev vsp_cdev;
static struct class_device vsp_class_dev;

static struct hlist_head vsp_chan_index_hash[SB_CHAN_HASHSIZE];

static inline struct hlist_head *vsp_chan_index_get_hash(int index)
{
	return &vsp_chan_index_hash[index & (SB_CHAN_HASHSIZE - 1)];
}

static struct vsp_chan *__vsp_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct vsp_chan *vsp_chan;

	hlist_for_each_entry(vsp_chan, t, vsp_chan_index_get_hash(index),
			index_hlist_node) {
		if (vsp_chan->index == index)
			return vsp_chan;
	}

	return NULL;
}

static int vsp_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__vsp_chan_get_by_index(index))
			return index;
	}
}

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
	struct visdn_chan *visdn_chan,
	void *buf, size_t count)
{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	return __kfifo_get(chan->tx_fifo, buf, count);
}

static ssize_t vsp_chan_write(
	struct visdn_chan *visdn_chan,
	const void *buf, size_t count)

{
	struct vsp_chan *chan = to_vsp_chan(visdn_chan);

	return __kfifo_put(chan->rx_fifo, (void *)buf, count);
}

static int vsp_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	vsp_debug(2, "Streamport %s connected to %s\n",
		visdn_chan->cxc_id,
		visdn_chan2->cxc_id);

	return 0;
}

static int vsp_chan_disconnect(struct visdn_chan *visdn_chan)
{
	vsp_debug(2, "Streamport %s disconnected\n",
		visdn_chan->cxc_id);

	return 0;
}

static struct visdn_chan_ops vsp_chan_ops = {
	.owner		= THIS_MODULE,
	.release	= vsp_chan_release,
	.open		= vsp_chan_open,
	.close		= vsp_chan_close,
	.frame_xmit	= NULL,
	.get_stats	= NULL,

        .connect_to	= vsp_chan_connect_to,
	.disconnect	= vsp_chan_disconnect,

	.read		= vsp_chan_read,
	.write		= vsp_chan_write,
};

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

static int vsp_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;

	nonseekable_open(inode, file);

	struct vsp_chan *chan;
	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(chan, 0, sizeof(*chan));

	spin_lock_init(&chan->rx_fifo_lock);
	chan->rx_fifo = kfifo_alloc(16384, GFP_KERNEL, &chan->rx_fifo_lock);
	if (!chan->rx_fifo) {
		err = -EFAULT;
		goto err_fifo_rx_alloc;
	}

	spin_lock_init(&chan->tx_fifo_lock);
	chan->tx_fifo = kfifo_alloc(16384, GFP_KERNEL, &chan->tx_fifo_lock);
	if (!chan->tx_fifo) {
		err = -EFAULT;
		goto err_fifo_tx_alloc;
	}

	visdn_chan_init(&chan->visdn_chan);

	chan->index = vsp_chan_new_index();

	chan->visdn_chan.ops = &vsp_chan_ops;
	chan->visdn_chan.port = &vsp_port;
	chan->visdn_chan.cxc = &visdn_int_cxc.cxc;

	snprintf(chan->visdn_chan.name, sizeof(chan->visdn_chan.name),
		"%d", chan->index);

	chan->visdn_chan.driver_data = chan;
	chan->visdn_chan.externally_managed = TRUE;

	chan->visdn_chan.max_mtu = 0;
	chan->visdn_chan.bitrate_selection = VISDN_CHAN_BITRATE_SELECTION_MAX;
	chan->visdn_chan.bitrates_cnt = 0;
	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_TRANS;
	chan->visdn_chan.framing_preferred = VISDN_CHAN_FRAMING_TRANS;
	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_MSB;
	chan->visdn_chan.bitorder_preferred = 0;

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

	vsp_debug(2, "Streamport %d opened\n", chan->index);

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
	vsp_debug(3, "vsp_cdev_release()\n");

	BUG_ON(!file->private_data);

	struct vsp_chan *chan = file->private_data;

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
	int err;

	BUG_ON(!file->private_data);

	struct vsp_chan *chan = file->private_data;

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

	BUG_ON(!file->private_data);

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

static inline int vsp_cdev_do_ioctl_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;

	struct vsp_chan *chan = file->private_data;

	struct visdn_connect connect;

 	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	vsp_debug(2, "ioctl(IOC_CONNECT, '%s', '%s')\n",
		connect.src_chanid,
		connect.dst_chanid);

	struct visdn_chan *visdn_chan2 =
		visdn_cxc_search_chan(&visdn_int_cxc.cxc,
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

	// Release reference returned by visdn_cxc_search_chan()
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

static inline int vsp_cdev_do_ioctl_disconnect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vsp_chan *chan = file->private_data;

	vsp_debug(2, "ioctl(IOC_DISCONNECT)\n");

	visdn_disconnect(&chan->visdn_chan);

	visdn_pass_close(&chan->visdn_chan);

	return 0;
}

static int vsp_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return vsp_cdev_do_ioctl_connect(inode, file, cmd, arg);
	break;
	case VISDN_IOC_DISCONNECT:
		return vsp_cdev_do_ioctl_disconnect(inode, file, cmd, arg);
	break;
	}

	return -EINVAL;
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

	int i;
	for (i=0; i< ARRAY_SIZE(vsp_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&vsp_chan_index_hash[i]);
	}

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

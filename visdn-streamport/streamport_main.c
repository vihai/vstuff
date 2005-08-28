/*
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/list.h>

#include "streamport.h"

static dev_t vsp_first_dev;
static struct cdev vsp_cdev;
static struct class_device vsp_class_dev;

struct hlist_head vsp_chan_index_hash[SB_CHAN_HASHSIZE];

static inline struct hlist_head *vsp_chan_index_get_hash(int index)
{
	return &vsp_chan_index_hash[index & (SB_CHAN_HASHSIZE - 1)];
}

struct vsp_chan *__vsp_chan_get_by_index(int index)
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

struct visdn_port vsp_visdn_port;

static void vsp_chan_release(struct visdn_chan *visdn_chan)
{
	kfree(to_vsp_chan(visdn_chan));
}

static int vsp_chan_open(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vsp_open()\n");

	return 0;
}

static int vsp_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vsp_close()\n");

	return 0;
}

static int vsp_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan2->connected_chan ||
	    visdn_chan->connected_chan)
		return -EBUSY;

	printk(KERN_INFO "Streamport %s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	return 0;
}

static int vsp_chan_disconnect(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan->connected_chan)
		return 0;

	printk(KERN_INFO "Streamport %s disconnected from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	return 0;
}

struct visdn_chan_ops vsp_chan_ops = {
	.release	= vsp_chan_release,
	.open		= vsp_chan_open,
	.close		= vsp_chan_close,
	.frame_xmit	= NULL,
	.get_stats	= NULL,
	.do_ioctl	= NULL,

        .connect_to     = vsp_chan_connect_to,
	.disconnect	= vsp_chan_disconnect,

	.samples_read   = NULL, // This should be implemented for user<=>user
	.samples_write  = NULL,
};

int vsp_cdev_open(
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

	visdn_chan_init(&chan->visdn_chan, &vsp_chan_ops);

	chan->index = vsp_chan_new_index();

	chan->visdn_chan.priv = chan;
	chan->visdn_chan.speed = 0;
	chan->visdn_chan.role = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.roles = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.flags = 0;

	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_TRANS;
	chan->visdn_chan.framing_preferred = VISDN_CHAN_FRAMING_TRANS;

	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_MSB;
	chan->visdn_chan.bitorder_preferred = 0;

	char chanid[10];
	snprintf(chanid, sizeof(chanid), "%d", chan->index);

	err = visdn_chan_register(&chan->visdn_chan, chanid,
		&vsp_visdn_port);
	if (err < 0)
		goto err_chan_register;

	file->private_data = chan;

	printk(KERN_WARNING "Streamport %d opened\n", chan->index);

	return 0;

	visdn_chan_unregister(&chan->visdn_chan);
err_chan_register:
	kfree(chan);
err_kmalloc:

	return err;
}

int vsp_cdev_release(
	struct inode *inode, struct file *file)
{
	BUG_ON(!file->private_data);

	struct vsp_chan *chan = file->private_data;

	visdn_chan_unregister(&chan->visdn_chan);

	return 0;
}

ssize_t vsp_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

	struct vsp_chan *chan = file->private_data;

	if (!chan->visdn_chan.connected_chan)
		return -ENOTCONN;

	if (!chan->visdn_chan.connected_chan->ops->samples_read)
		return -EOPNOTSUPP;

	return chan->visdn_chan.connected_chan->ops->samples_read(
			chan->visdn_chan.connected_chan, buf, count);
}

ssize_t vsp_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

	struct vsp_chan *chan = file->private_data;

	if (!chan->visdn_chan.connected_chan)
		return -ENOTCONN;

	if (!chan->visdn_chan.connected_chan->ops->samples_write)
		return -EOPNOTSUPP;

	return chan->visdn_chan.connected_chan->ops->samples_write(
			chan->visdn_chan.connected_chan,
			buf, count);
}

#define SB_IOC_FIFO_IN_AVAIL	_IOR(0xd0, 1, int)


static inline int visdn_cdev_do_ioctl_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;

	struct vsp_chan *chan = file->private_data;
	struct visdn_chan *chan1 = &chan->visdn_chan;

	BUG_ON(!chan1);

	struct visdn_connect connect;

 	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	printk(KERN_INFO "ioctl(IOC_CONNECT, '%s', '%s')\n",
		connect.src_chanid,
		connect.dst_chanid);

	struct visdn_chan *chan2 = visdn_search_chan(connect.dst_chanid);
	if (!chan2) {
		err = -ENODEV;
		goto err_search_dst;
	}

	printk(KERN_ERR "chan2 found, %s %p %p \n",chan2->device.bus_id, chan2, chan2->ops);

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_connect_self;
	}

	err = visdn_connect(chan1, chan2, connect.flags);
	if (err < 0)
		goto err_connect;

	// Release reference returned by visdn_search_chan()
	put_device(&chan2->device);

	return 0;

//	visdn_disconnect()
err_connect:
err_connect_self:
	put_device(&chan2->device);
err_search_dst:
err_copy_from_user:

	return err;
}

int vsp_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return visdn_cdev_do_ioctl_connect(inode, file, cmd, arg);
	break;
	}

	return -EINVAL;
}

struct file_operations vsp_fops =
{
	.owner		= THIS_MODULE,
	.read		= vsp_cdev_read,
	.write		= vsp_cdev_write,
	.ioctl		= vsp_cdev_ioctl,
	.open		= vsp_cdev_open,
	.release	= vsp_cdev_release,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops vsp_port_ops = {
	.enable		= NULL,
	.disable	= NULL,
};

static int __init vsp_init_module(void)
{
	int err;

	printk(KERN_INFO vsp_MODULE_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(vsp_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&vsp_chan_index_hash[i]);
	}

	visdn_port_init(&vsp_visdn_port, &vsp_port_ops);
	err = visdn_port_register(
		&vsp_visdn_port,
		vsp_MODULE_NAME, vsp_MODULE_NAME,
		&visdn_system_device);
	if (err < 0)
		goto err_visdn_port_register;

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
	vsp_class_dev.dev = &vsp_visdn_port.device;
	vsp_class_dev.devt = vsp_first_dev;

	err = class_device_register(&vsp_class_dev);
	if (err < 0) {
		// TODO FIXME
	}

	return 0;

	visdn_port_unregister(&vsp_visdn_port);
err_visdn_port_register:
	cdev_del(&vsp_cdev);
err_cdev_add:
	unregister_chrdev_region(vsp_first_dev, 1);
err_register_chrdev:

	return err;
}

module_init(vsp_init_module);

static void __exit vsp_module_exit(void)
{
	class_device_unregister(&vsp_class_dev);
	visdn_port_unregister(&vsp_visdn_port);
	cdev_del(&vsp_cdev);
	unregister_chrdev_region(vsp_first_dev, 1);

	printk(KERN_INFO vsp_MODULE_DESCR " unloaded\n");
}

module_exit(vsp_module_exit);

MODULE_DESCRIPTION(vsp_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

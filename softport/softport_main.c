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
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include "softport.h"

static dev_t sb_first_dev;
static struct cdev sb_cdev;

struct sb_chan chans[256];
struct visdn_port sb_visdn_port;


struct hlist_head sb_chan_index_hash[1 << SB_CHAN_HASHBITS];

static inline struct hlist_head *sb_chan_index_get_hash(int index)
{
	return &sb_chan_index_hash[index & ((1 << SB_CHAN_HASHBITS) - 1)];
}

struct sb_chan *__sb_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct sb_chan *sb_chan;

	hlist_for_each_entry(sb_chan, t, sb_chan_index_get_hash(index),
			index_hlist) {
		if (sb_chan->index == index)
			return sb_chan;
	}

	return NULL;
}

static int sb_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__sb_chan_get_by_index(index))
			return index;
	}
}


struct sb_cdev_data
{
	struct sb_chan *chan;
};

int sb_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	int chan_idx = inode->i_rdev - sb_first_dev;

	if (chan_idx > 256 || chan_idx < 0)
		return -ENODEV;

	file->private_data = &chans[chan_idx];

	printk(KERN_WARNING "SOFTPORT %d opened\n", chan_idx);

	return 0;
}

int sb_cdev_release(
	struct inode *inode, struct file *file)
{
	BUG_ON(!file->private_data);

	return 0;
}

ssize_t sb_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

	struct sb_chan *chan = file->private_data;

	if (!chan->visdn_chan.connected_chan)
		return -ENOTCONN;

	if (!chan->visdn_chan.connected_chan->ops->samples_read)
		return -EOPNOTSUPP;

	return chan->visdn_chan.connected_chan->ops->samples_read(
			chan->visdn_chan.connected_chan, buf, count);
}

ssize_t sb_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	BUG_ON(!file->private_data);

	struct sb_chan *chan = file->private_data;

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
	struct visdn_connect connect;

	struct visdn_chan *chan1 = file->private_data;

 	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	struct visdn_chan *chan2 = visdn_search_chan(connect.dst_chanid);
	if (!chan2) {
		err = -ENODEV;
		goto err_search_dst;
	}

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

err_connect:
//	visdn_disconnect()
err_connect_self:
	put_device(&chan1->device);
err_search_dst:
	put_device(&chan2->device);
err_search_src:
err_copy_from_user:

	return err;
}

ssize_t sb_cdev_ioctl(
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

struct file_operations sb_fops =
{
	.owner		= THIS_MODULE,
	.read		= sb_cdev_read,
	.write		= sb_cdev_write,
	.ioctl		= sb_cdev_ioctl,
	.open		= sb_cdev_open,
	.release	= sb_cdev_release,
	.llseek		= no_llseek,
};

static struct class_device_attribute *sb_class_attributes[] = {
	NULL
};

#define to_chan(class) container_of(class, struct sb_chan, class_dev)

static int sb_hotplug(struct class_device *cd, char **envp,
	int num_envp, char *buf, int size)
{
	envp[0] = NULL;

	return 0;
}

static void sb_release(struct class_device *cd)
{
	printk(KERN_DEBUG sb_DRIVER_PREFIX "sb_release called\n");

	// kfree ??
}

static struct class sb_class = {
	.name = "softport",
	.release = sb_release,
#ifdef CONFIG_HOTPLUG
	.hotplug = sb_hotplug,
#endif
};

static int sb_open(struct visdn_chan *visdn_chan, int mode)
{
	printk(KERN_INFO "sb_open()\n");

	return -EINVAL;
}

static int sb_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "sb_close()\n");

	return -EINVAL;
}

static int sb_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	get_device(&visdn_chan2->device);

	visdn_chan->connected_chan = visdn_chan2;

	sysfs_create_link(
		&visdn_chan->device.kobj,
		&visdn_chan2->device.kobj,
		"connected");

	printk(KERN_INFO "Softport %s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	return 0;
}

static int sb_disconnect(struct visdn_chan *visdn_chan)
{
	put_device(&visdn_chan->device);

	visdn_chan->connected_chan = NULL;

	sysfs_remove_link(&visdn_chan->device.kobj, "connected");

	return 0;
}

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops sb_port_ops = {
	.set_role	= NULL,
};

struct visdn_chan_ops sb_chan_ops = {
	.open		= sb_open,
	.close		= sb_close,
	.frame_xmit	= NULL,
	.get_stats	= NULL,
	.do_ioctl	= NULL,

        .connect_to     = sb_connect_to,
	.disconnect	= sb_disconnect,

	.samples_read   = NULL,
	.samples_write  = NULL,
};

static int __devinit sb_probe(struct device *dev)
{
	printk(KERN_INFO "######## sb_probe\n");

	return 0;
}

static int __devexit sb_remove(struct device *dev)
{
	int i;

	printk(KERN_INFO "######## sb_remove\n");

	for (i=0; i<256; i++) {
	}

	return 0;
}

static void sb_device_release(struct device *device)
{
	printk(KERN_DEBUG sb_DRIVER_PREFIX "sb_device_release called\n");
}

static struct device_driver sb_driver = {
	.name           = sb_DRIVER_NAME,
	.bus            = NULL,
	.probe          = sb_probe,
	.remove         = sb_remove,
	.suspend        = NULL,
	.resume         = NULL,
};

static struct device sb_device =
{
	.bus_id		= "softport",
	.release	= sb_device_release,
};

static struct bus_type sb_bus =
{
	.name		= "softport",
};

static int __init sb_init_module(void)
{
	int err;

	printk(KERN_INFO sb_DRIVER_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(sb_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&sb_chan_index_hash[i]);
	}

	err = bus_register(&sb_bus);
	if (err < 0)
		goto err_bus_register;

	err = driver_register(&sb_driver);
	if (err < 0)
		goto err_driver_register;

	sb_device.bus = &sb_bus;
	err = device_register(&sb_device);
	if (err < 0)
		goto err_device_register;

	err = class_register(&sb_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&sb_first_dev, 0, 256, sb_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&sb_cdev, &sb_fops);
	sb_cdev.owner = THIS_MODULE;

	err = cdev_add(&sb_cdev, sb_first_dev, 256);
	if (err < 0)
		goto err_cdev_add;

	visdn_port_init(&sb_visdn_port, &sb_port_ops);
	err = visdn_port_register(&sb_visdn_port, "softport", &sb_device);
	if (err < 0)
		goto err_visdn_port_register;

	for (i=0; i<256; i++) {
		INIT_HLIST_NODE(&chans[i].index_hlist);

		chans[i].index = sb_chan_new_index();

		visdn_chan_init(&chans[i].visdn_chan, &sb_chan_ops);
		chans[i].visdn_chan.priv = &chans[i];
		chans[i].visdn_chan.speed = 0;
		chans[i].visdn_chan.role = VISDN_CHAN_ROLE_B;
		chans[i].visdn_chan.roles = VISDN_CHAN_ROLE_B;
		chans[i].visdn_chan.protocol = 0;
		chans[i].visdn_chan.flags = 0;

		char chanid[10];
		snprintf(chanid, sizeof(chanid), "%d", i);

		visdn_chan_register(&chans[i].visdn_chan, chanid,
			&sb_visdn_port);

		snprintf(chans[i].class_dev.class_id,
			sizeof(chans[i].class_dev.class_id),
			"%d", i);
		chans[i].class_dev.class = &sb_class;
		chans[i].class_dev.dev =  &chans[i].visdn_chan.device;
		chans[i].class_dev.devt = sb_first_dev + i;

		err = class_device_register(&chans[i].class_dev);
		if (err < 0) {
			// TODO FIXME
		}
	}

	return 0;

	visdn_port_unregister(&sb_visdn_port);
err_visdn_port_register:
	cdev_del(&sb_cdev);
err_cdev_add:
	unregister_chrdev_region(sb_first_dev, 256);
err_register_chrdev:
	class_unregister(&sb_class);
err_class_register:
	device_unregister(&sb_device);
err_device_register:
	driver_unregister(&sb_driver);
err_driver_register:
	bus_unregister(&sb_bus);
err_bus_register:

	return err;
}

module_init(sb_init_module);

static void __exit sb_module_exit(void)
{
	int i;

	for (i=0; i<256; i++) {
		visdn_chan_unregister(&chans[i].visdn_chan);
	}

	for (i=0; i<256; i++) {
		class_device_unregister(&chans[i].class_dev);
	}

	visdn_port_unregister(&sb_visdn_port);
	cdev_del(&sb_cdev);
	unregister_chrdev_region(sb_first_dev, 256);
	class_unregister(&sb_class);
	device_unregister(&sb_device);
	driver_unregister(&sb_driver);
	bus_unregister(&sb_bus);

	printk(KERN_INFO sb_DRIVER_DESCR " unloaded\n");
}

module_exit(sb_module_exit);

MODULE_DESCRIPTION(sb_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

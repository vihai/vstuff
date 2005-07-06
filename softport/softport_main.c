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
#include <linux/list.h>

#include "softport.h"

static dev_t sb_first_dev;
static struct cdev sb_cdev;
static struct class_device sb_class_dev;

struct hlist_head sb_chan_index_hash[SB_CHAN_HASHSIZE];

static inline struct hlist_head *sb_chan_index_get_hash(int index)
{
	return &sb_chan_index_hash[index & (SB_CHAN_HASHSIZE - 1)];
}

struct sb_chan *__sb_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct sb_chan *sb_chan;

	hlist_for_each_entry(sb_chan, t, sb_chan_index_get_hash(index),
			index_hlist_node) {
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

struct visdn_port sb_visdn_port;

static void sb_chan_release(struct visdn_chan *visdn_chan)
{
	kfree(to_sb_chan(visdn_chan));
}

static int sb_chan_open(struct visdn_chan *visdn_chan, int mode)
{
	printk(KERN_INFO "sb_open()\n");

	return -EINVAL;
}

static int sb_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "sb_close()\n");

	return -EINVAL;
}

static int sb_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan2->connected_chan ||
	    visdn_chan->connected_chan)
		return -EBUSY;

	printk(KERN_INFO "Softport %s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	return 0;
}

static int sb_chan_disconnect(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan->connected_chan)
		return 0;

	printk(KERN_INFO "Softport %s disconnected from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	return 0;
}

struct visdn_chan_ops sb_chan_ops = {
	.release	= sb_chan_release,
	.open		= sb_chan_open,
	.close		= sb_chan_close,
	.frame_xmit	= NULL,
	.get_stats	= NULL,
	.do_ioctl	= NULL,

        .connect_to     = sb_chan_connect_to,
	.disconnect	= sb_chan_disconnect,

	.samples_read   = NULL,
	.samples_write  = NULL,
};

int sb_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;

	nonseekable_open(inode, file);

	struct sb_chan *chan;
	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	visdn_chan_init(&chan->visdn_chan, &sb_chan_ops);

	chan->index = sb_chan_new_index();

	chan->visdn_chan.priv = chan;
	chan->visdn_chan.speed = 0;
	chan->visdn_chan.role = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.roles = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.protocol = 0;
	chan->visdn_chan.flags = 0;

	char chanid[10];
	snprintf(chanid, sizeof(chanid), "%d", chan->index);

	err = visdn_chan_register(&chan->visdn_chan, chanid,
		&sb_visdn_port);
	if (err < 0)
		goto err_chan_register;

	file->private_data = chan;

	printk(KERN_WARNING "SOFTPORT %d opened\n", chan->index);

	return 0;

	visdn_chan_unregister(&chan->visdn_chan);
err_chan_register:
	kfree(chan);
err_kmalloc:

	return err;
}

int sb_cdev_release(
	struct inode *inode, struct file *file)
{
	BUG_ON(!file->private_data);

	struct sb_chan *chan = file->private_data;

	visdn_chan_unregister(&chan->visdn_chan);

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

	struct sb_chan *chan = file->private_data;
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

static int sb_class_hotplug(struct class_device *cd, char **envp,
	int num_envp, char *buf, int size)
{
	envp[0] = NULL;

	return 0;
}

static void sb_class_release(struct class_device *cd)
{
	printk(KERN_DEBUG sb_DRIVER_PREFIX "sb_class_release called\n");
}

static struct class sb_class = {
	.name = "visdn_softport",
	.release = sb_class_release,
#ifdef CONFIG_HOTPLUG
	.hotplug = sb_class_hotplug,
#endif
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops sb_port_ops = {
	.set_role	= NULL,
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

/*static void sb_device_release(struct device *device)
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

static struct bus_type sb_bus =
{
	.name		= "softport",
};

static struct device sb_device =
{
	.bus_id		= "softport",
	.release	= sb_device_release,
	.bus		= &sb_bus,
};*/

static int __init sb_init_module(void)
{
	int err;

	printk(KERN_INFO sb_DRIVER_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(sb_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&sb_chan_index_hash[i]);
	}

/*	err = bus_register(&sb_bus);
	if (err < 0)
		goto err_bus_register;

	
	err = driver_register(&sb_driver);
	if (err < 0)
		goto err_driver_register;

	err = device_register(&sb_device);
	if (err < 0)
		goto err_device_register;
*/

	err = class_register(&sb_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&sb_first_dev, 0, 1, sb_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&sb_cdev, &sb_fops);
	sb_cdev.owner = THIS_MODULE;

	err = cdev_add(&sb_cdev, sb_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	snprintf(sb_class_dev.class_id,
		sizeof(sb_class_dev.class_id),
		"softport");
	sb_class_dev.class = &sb_class;
	sb_class_dev.dev =  NULL; // sb_device
	sb_class_dev.devt = sb_first_dev;

	err = class_device_register(&sb_class_dev);
	if (err < 0) {
		// TODO FIXME
	}

	visdn_port_init(&sb_visdn_port, &sb_port_ops);
	err = visdn_port_register(&sb_visdn_port, "softport", NULL); // &sb_device
	if (err < 0)
		goto err_visdn_port_register;

	return 0;

	visdn_port_unregister(&sb_visdn_port);
err_visdn_port_register:
	cdev_del(&sb_cdev);
err_cdev_add:
	unregister_chrdev_region(sb_first_dev, 1);
err_register_chrdev:
	class_unregister(&sb_class);
err_class_register:
//	device_unregister(&sb_device);
//err_device_register:
//	driver_unregister(&sb_driver);
//err_driver_register:
//	bus_unregister(&sb_bus);
//err_bus_register:

	return err;
}

module_init(sb_init_module);

static void __exit sb_module_exit(void)
{
	class_device_unregister(&sb_class_dev);
	visdn_port_unregister(&sb_visdn_port);
	cdev_del(&sb_cdev);
	unregister_chrdev_region(sb_first_dev, 1);
	class_unregister(&sb_class);
//	device_unregister(&sb_device);
//	driver_unregister(&sb_driver);
//	bus_unregister(&sb_bus);

	printk(KERN_INFO sb_DRIVER_DESCR " unloaded\n");
}

module_exit(sb_module_exit);

MODULE_DESCRIPTION(sb_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

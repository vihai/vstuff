/*
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 * Please read the README file for important infos.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include "visdn.h"
#include "visdn_mod.h"

#include "chan.h"
#include "port.h"
#include "timer.h"

dev_t visdn_first_dev;
static struct cdev visdn_ctl_cdev;

static int visdn_ctl_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

static int visdn_ctl_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

static inline int visdn_ctl_do_ioctl_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct visdn_connect connect;

 	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	printk(KERN_INFO "ioctl(IOC_CONNECT, '%s', '%s'\n",
		connect.src_chanid,
		connect.dst_chanid);

	struct visdn_chan *chan1 = visdn_search_chan(connect.src_chanid);
	if (!chan1) {
		err = -ENODEV;
		goto err_search_src;
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

	// Release references returned by visdn_search_chan()
	put_device(&chan1->device);
	put_device(&chan2->device);

	return 0;

//	visdn_disconnect()
err_connect:
err_connect_self:
	put_device(&chan2->device);
err_search_dst:
	put_device(&chan1->device);
err_search_src:
err_copy_from_user:

	return err;
}

int visdn_ctl_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return visdn_ctl_do_ioctl_connect(inode, file, cmd, arg);
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
EXPORT_SYMBOL(visdn_ctl_ioctl);

struct file_operations visdn_ctl_fops =
{
	.owner		= THIS_MODULE,
	.ioctl		= visdn_ctl_ioctl,
	.open		= visdn_ctl_open,
	.release	= visdn_ctl_release,
	.llseek		= no_llseek,
};





static int visdn_hotplug(struct class_device *cd, char **envp,
	int num_envp, char *buf, int size)
{
	envp[0] = NULL;

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_hotplug called\n");

	return 0;
}

static void visdn_release(struct class_device *cd)
{
}

static struct class visdn_system_class = {
	.name = "visdn",
	.release = visdn_release,
	.hotplug = visdn_hotplug,
};
EXPORT_SYMBOL(visdn_system_class);

static struct class_device visdn_control_class_dev;

/******************************************
 * Module stuff
 ******************************************/

#ifdef NO_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, visdn_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static void visdn_system_device_release(struct device *cd)
{
}

struct device visdn_system_device;
EXPORT_SYMBOL(visdn_system_device);

static int __init visdn_init_module(void)
{
	int err;

	printk(KERN_INFO visdn_MODULE_DESCR " loading\n");

	err = alloc_chrdev_region(&visdn_first_dev, 0, 2, visdn_MODULE_NAME);
	if (err < 0)
		goto err_alloc_chrdev_region;

	cdev_init(&visdn_ctl_cdev, &visdn_ctl_fops);
	visdn_ctl_cdev.owner = THIS_MODULE;
	err = cdev_add(&visdn_ctl_cdev, visdn_first_dev + 0, 1);
	if (err < 0)
		goto err_cdev_add;

	visdn_system_device.bus = NULL;
	visdn_system_device.parent = NULL;
	visdn_system_device.driver_data = NULL;
	visdn_system_device.release = visdn_system_device_release;

	snprintf(visdn_system_device.bus_id,
		sizeof(visdn_system_device.bus_id),
		"visdn-system");

	err = device_register(&visdn_system_device);
	if (err < 0)
		goto err_system_device_register;

	err = class_register(&visdn_system_class);
	if (err < 0)
		goto err_class_register;

	class_device_initialize(&visdn_control_class_dev);
	visdn_control_class_dev.class = &visdn_system_class;
	visdn_control_class_dev.class_data = NULL;
	visdn_control_class_dev.dev = &visdn_system_device;
#ifndef NO_CLASS_DEV_DEVT
	visdn_control_class_dev.devt = visdn_first_dev;
#endif
	snprintf(visdn_control_class_dev.class_id,
		sizeof(visdn_control_class_dev.class_id),
		"control");

	err = class_device_register(&visdn_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifdef NO_CLASS_DEV_DEVT
	class_device_create_file(
		&visdn_control_class_dev,
		&class_device_attr_dev);
#endif

	err = visdn_timer_modinit();
	if (err < 0)
		goto err_timer_modinit;

	err = visdn_port_modinit();
	if (err < 0)
		goto err_port_modinit;

	err = visdn_chan_modinit();
	if (err < 0)
		goto err_chan_modinit;

	return 0;

	visdn_chan_modexit();
err_chan_modinit:
	visdn_port_modexit();
err_port_modinit:
	visdn_timer_modexit();
err_timer_modinit:
	class_device_del(&visdn_control_class_dev);
err_control_class_device_register:
	class_unregister(&visdn_system_class);
err_class_register:
	device_unregister(&visdn_system_device);
err_system_device_register:
	cdev_del(&visdn_ctl_cdev);
err_cdev_add:
	unregister_chrdev_region(visdn_first_dev, 2);
err_alloc_chrdev_region:

	return err;
}

module_init(visdn_init_module);

static void __exit visdn_module_exit(void)
{
	visdn_chan_modexit();
	visdn_port_modexit();
	visdn_timer_modexit();

	class_device_del(&visdn_control_class_dev);
	class_unregister(&visdn_system_class);

	device_unregister(&visdn_system_device);

	cdev_del(&visdn_ctl_cdev);
	unregister_chrdev_region(visdn_first_dev, 2);

	printk(KERN_INFO visdn_MODULE_DESCR " unloaded\n");
}

module_exit(visdn_module_exit);

MODULE_DESCRIPTION(visdn_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

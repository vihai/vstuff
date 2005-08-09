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
#include "timer.h"

static struct cdev visdn_timer_cdev;
static struct class visdn_timer_class;

int visdn_timer_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	// TODO FIXME: Use minor to select correct timer

	struct class_device *device;
	down_write(&visdn_timer_class.subsys.rwsem);
	list_for_each_entry(device, &visdn_timer_class.children, node) {
		printk(KERN_DEBUG "------------> %s\n", device->kobj.name);

		file->private_data = to_visdn_timer(device);
		to_visdn_timer(device)->file = file;
		break;
	}
	up_write(&visdn_timer_class.subsys.rwsem);

	return 0;
}

int visdn_timer_cdev_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t visdn_timer_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	}

	return -EINVAL;
}


static unsigned int visdn_timer_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	BUG_ON(!file->private_data);

	struct visdn_timer *timer = file->private_data;

	BUG_ON(!timer->ops);

	return timer->ops->poll(timer, wait);
}

struct file_operations visdn_timer_fops =
{
	.owner		= THIS_MODULE,
	.ioctl		= visdn_timer_cdev_ioctl,
	.open		= visdn_timer_cdev_open,
	.release	= visdn_timer_cdev_release,
	.poll		= visdn_timer_cdev_poll,
	.llseek		= no_llseek,
};

static struct hlist_head visdn_timer_index_hash[1 << VISDN_PORT_HASHBITS];

static ssize_t visdn_timer_show_freq(
	struct class_device *dev,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", HZ);
}

static ssize_t visdn_timer_store_freq(
	struct class_device *dev,
	const char *buf,
	size_t count)
{
	//chan->netdev->dev_addr[0] = 0x01;

	return -EINVAL;
}

static CLASS_DEVICE_ATTR(timer_freq, S_IRUGO,
		visdn_timer_show_freq,
		NULL);

static int visdn_timer_hotplug(struct class_device *cd, char **envp,
	int num_envp, char *buf, int size)
{
//	struct visdn_timer *visdn_timer = to_visdn_timer(cd);

	envp[0] = NULL;

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_timer_hotplug called\n");

	return 0;
}

static void visdn_timer_release(struct class_device *cd)
{
//	struct visdn_timer *visdn_timer =
//		container_of(cd, struct visdn_timer, class_dev);

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_timer_release called\n");

	// kfree ??
}

static struct class visdn_timer_class = {
	.name = "visdn_timer",
	.release = visdn_timer_release,
	.hotplug = visdn_timer_hotplug,
};

struct visdn_timer *visdn_timer_alloc(void)
{
	struct visdn_timer *isdn_port;

	isdn_port = kmalloc(sizeof(*isdn_port), GFP_KERNEL);
	if (!isdn_port)
		return NULL;

	memset(isdn_port, 0x00, sizeof(*isdn_port));

	return isdn_port;
}
EXPORT_SYMBOL(visdn_timer_alloc);

void visdn_timer_init(
	struct visdn_timer *visdn_timer,
	struct visdn_timer_ops *ops)
{
	BUG_ON(!visdn_timer);
	BUG_ON(!ops);

	memset(visdn_timer, 0x00, sizeof(*visdn_timer));
	visdn_timer->ops = ops;
}
EXPORT_SYMBOL(visdn_timer_init);

int visdn_timer_register(
	struct visdn_timer *visdn_timer,
	const char *name)
{
	int err;

	BUG_ON(!visdn_timer);
	BUG_ON(!name);

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_timer_register(%s) called\n", name);

	struct class_device *class_dev = &visdn_timer->class_dev;

	memset(class_dev, 0x00, sizeof(class_dev));
	class_dev->class = &visdn_timer_class;
	class_dev->class_data = visdn_timer;
	class_dev->devt = visdn_first_dev + 1;

	snprintf(class_dev->class_id, sizeof(class_dev->class_id),
		"%s", name);

	err = class_device_register(class_dev);
	if (err < 0)
		goto err_class_device_register;

	err = class_device_create_file(
			class_dev,
			&class_device_attr_timer_freq);
	if (err < 0)
		goto err_class_device_create_file;

	return 0;

err_class_device_create_file:
	class_device_del(class_dev);
err_class_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_timer_register);

void visdn_timer_unregister(
	struct visdn_timer *visdn_timer)
{
	struct class_device *class_dev = &visdn_timer->class_dev;

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_timer_unregister called\n");

	class_device_del(class_dev);
}
EXPORT_SYMBOL(visdn_timer_unregister);

int visdn_timer_modinit(void)
{
	int err;

	err = class_register(&visdn_timer_class);
	if (err < 0)
		goto err_class_register;

	cdev_init(&visdn_timer_cdev, &visdn_timer_fops);
	visdn_timer_cdev.owner = THIS_MODULE;
	err = cdev_add(&visdn_timer_cdev, visdn_first_dev + 1, 1);
	if (err < 0)
		goto err_cdev_add;

	return 0;

err_cdev_add:
	class_unregister(&visdn_timer_class);
err_class_register:

	return err;

}

void visdn_timer_modexit(void)
{
	cdev_del(&visdn_timer_cdev);

	class_unregister(&visdn_timer_class);
}

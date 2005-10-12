/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include "visdn.h"
#include "visdn_mod.h"
#include "timer.h"
#include "cxc.h"

static struct cdev visdn_timer_cdev;
static struct class visdn_timer_class;

struct visdn_timer_user
{
	struct visdn_timer *timer;
	struct file *file;
};

int visdn_timer_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct visdn_timer *timer = NULL;

	visdn_debug(3, "visdn_timer_cdev_open()\n");

	nonseekable_open(inode, file);

	// TODO FIXME: Use minor to select correct timer

	struct class_device *device;
	down_write(&visdn_timer_class.subsys.rwsem);
	list_for_each_entry(device, &visdn_timer_class.children, node) {
		timer = to_visdn_timer(device);
		break;
	}
	up_write(&visdn_timer_class.subsys.rwsem);

	struct visdn_timer_user *timer_user;
	timer_user = kmalloc(sizeof(*timer_user), GFP_KERNEL);
	if (!timer_user) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(timer_user, 0, sizeof(*timer_user));
	timer_user->timer = timer;
	timer_user->file = file;

	file->private_data = timer_user;

	return 0;

	kfree(timer_user);
err_kmalloc:

	return err;
}

int visdn_timer_cdev_release(
	struct inode *inode, struct file *file)
{
	visdn_debug(3, "visdn_timer_cdev_release()\n");

	struct visdn_timer_user *timer_user = file->private_data;

	kfree(timer_user);

	return 0;
}

int visdn_timer_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	visdn_debug(3, "visdn_timer_cdev_ioctl()\n");

	switch(cmd) {
	}

	return -EOPNOTSUPP;
}

void visdn_timer_tick(struct visdn_timer *timer)
{
	rcu_read_lock();
	struct visdn_cxc *cxc;
	list_for_each_entry_rcu(cxc, &visdn_cxc_list, cxc_list_node) {
		if (cxc->ops->timer_func)
			cxc->ops->timer_func(cxc);
	}
	rcu_read_unlock();

	timer->poll_count++;
	if (timer->poll_count >= timer->poll_divider) {
		timer->poll_count = 0;

		wake_up(&timer->wait_queue);
	}
}
EXPORT_SYMBOL(visdn_timer_tick);

static unsigned int visdn_timer_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	BUG_ON(!file->private_data);

	struct visdn_timer_user *timer_user = file->private_data;

	poll_wait(file, &timer_user->timer->wait_queue, wait);

	if (wait)
		return 0;
	else
		return POLLIN | POLLRDNORM;
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
		visdn_timer_store_freq);

static int visdn_timer_hotplug(struct class_device *device, char **envp,
	int num_envp, char *buf, int size)
{
//	struct visdn_timer *timer = to_visdn_timer(cd);

	visdn_debug(3, "visdn_timer_hotplug()\n");

	envp[0] = NULL;

	return 0;
}

static void visdn_timer_release(struct class_device *device)
{
	visdn_debug(3, "visdn_timer_release()\n");

	struct visdn_timer *timer = to_visdn_timer(device);

	BUG_ON(!timer->ops);

	if (timer->ops->release)
		timer->ops->release(timer);
}

static struct class visdn_timer_class = {
	.name = "visdn-timer",
	.release = visdn_timer_release,
	.hotplug = visdn_timer_hotplug,
};

void visdn_timer_init(
	struct visdn_timer *timer)
{
	BUG_ON(!timer);

	memset(timer, 0, sizeof(*timer));

	init_waitqueue_head(&timer->wait_queue);
}
EXPORT_SYMBOL(visdn_timer_init);

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, visdn_first_dev + 1);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

int visdn_timer_register(
	struct visdn_timer *timer)
{
	int err;

	BUG_ON(!timer);
	BUG_ON(!timer->name[0]);
	BUG_ON(!timer->ops);
	BUG_ON(!timer->ops->owner);

	visdn_debug(3, "visdn_timer_register(%s)\n", timer->name);

	struct class_device *class_dev = &timer->class_dev;

	memset(class_dev, 0, sizeof(class_dev));
	class_dev->class = &visdn_timer_class;
	class_dev->class_data = timer;
#ifdef HAVE_CLASS_DEV_DEVT
	class_dev->devt = visdn_first_dev + 1;
#endif

	snprintf(class_dev->class_id, sizeof(class_dev->class_id),
		"%s", timer->name);

	err = class_device_register(class_dev);
	if (err < 0)
		goto err_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		class_dev,
		&class_device_attr_dev);
#endif

	err = class_device_create_file(
			class_dev,
			&class_device_attr_timer_freq);
	if (err < 0)
		goto err_class_device_create_file;

#define TIMER_DIVIDER 100

	timer->main_divider = timer->natural_frequency / TIMER_DIVIDER;
	timer->poll_divider = TIMER_DIVIDER / 100;
	timer->poll_count = 0;

	if (timer->ops->open)
		timer->ops->open(timer);

	return 0;

err_class_device_create_file:
	class_device_del(class_dev);
err_class_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_timer_register);

void visdn_timer_unregister(
	struct visdn_timer *timer)
{
	struct class_device *class_dev = &timer->class_dev;

	visdn_debug(3, "visdn_timer_unregister()\n");

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
        	class_dev,
	        &class_device_attr_dev);
#endif

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

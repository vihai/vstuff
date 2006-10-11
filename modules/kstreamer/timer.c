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

#include "core.h"
#include "visdn_mod.h"
#include "timer.h"
#include "cxc.h"

static struct cdev visdn_timer_cdev;
static int visdn_timer_first_dev = 0;

struct list_head visdn_timer_list = LIST_HEAD_INIT(visdn_timer_list);
static spinlock_t visdn_timer_list_lock;

struct visdn_timer_user
{
	struct list_head users_list_node;

	struct visdn_timer *timer;
	struct file *file;

	int timer_reported;
};

int visdn_timer_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct visdn_timer *timer = NULL;
	struct visdn_timer_user *timer_user;

	visdn_debug(3, "visdn_timer_cdev_open()\n");

	nonseekable_open(inode, file);

	spin_lock(&visdn_timer_list_lock);
	list_for_each_entry(timer, &visdn_timer_list, timer_list_node) {
		if (timer->index == inode->i_rdev - visdn_timer_first_dev) {
			spin_unlock(&visdn_timer_list_lock);
			goto found;
		}
	}
	spin_unlock(&visdn_timer_list_lock);

	err = -ENODEV;
	goto err_timer_not_found;

found:;
	timer_user = kmalloc(sizeof(*timer_user), GFP_KERNEL);
	if (!timer_user) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(timer_user, 0, sizeof(*timer_user));
	timer_user->timer = timer;
	timer_user->file = file;

	file->private_data = timer_user;

	spin_lock_bh(&timer->users_list_lock);
	list_add_tail(&timer_user->users_list_node, &timer->users_list);
	spin_unlock_bh(&timer->users_list_lock);

	return 0;

	kfree(timer_user);
err_kmalloc:
err_timer_not_found:

	return err;
}

int visdn_timer_cdev_release(
	struct inode *inode, struct file *file)
{
	struct visdn_timer_user *timer_user = file->private_data;

	visdn_debug(3, "visdn_timer_cdev_release()\n");

	spin_lock_bh(&timer_user->timer->users_list_lock);
	list_del_init(&timer_user->users_list_node);
	spin_unlock_bh(&timer_user->timer->users_list_lock);

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

	return -EOPNOTSUPP;
}

void visdn_timer_tick(struct visdn_timer *timer)
{
	struct visdn_cxc *cxc;
	struct visdn_timer_user *timer_user;
//	cycles_t start_cycles = get_cycles();
	
	rcu_read_lock();
	list_for_each_entry_rcu(cxc, &visdn_cxc_list, cxc_list_node) {
		if (cxc->ops->timer_func)
			cxc->ops->timer_func(cxc);
	}
	rcu_read_unlock();

	timer->poll_count++;
	if (timer->poll_count >= timer->poll_divider) {
		timer->poll_count = 0;

		spin_lock_bh(&timer->users_list_lock);
		list_for_each_entry(timer_user, &timer->users_list,
							users_list_node) {
			timer_user->timer_reported = FALSE;
		}
		spin_unlock_bh(&timer->users_list_lock);

		wake_up(&timer->wait_queue);
	}

#if 0
	timer->tot_cycles += get_cycles() - start_cycles;
	timer->tot_ticks++;

	if (timer->tot_ticks >= 500) {
		printk(KERN_DEBUG "Timer: %llu/%llu\n",
			timer->tot_cycles, get_cycles() - timer->start_cycles);

		timer->tot_cycles = 0;
		timer->tot_ticks = 0;
		timer->start_cycles = get_cycles();
	}
#endif
}
EXPORT_SYMBOL(visdn_timer_tick);

static unsigned int visdn_timer_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	struct visdn_timer_user *timer_user = file->private_data;

	BUG_ON(!file->private_data);

	poll_wait(file, &timer_user->timer->wait_queue, wait);

	if (!timer_user->timer_reported)
		return POLLIN | POLLRDNORM;
	else
		return 0;
}

static ssize_t visdn_timer_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct visdn_timer_user *timer_user = file->private_data;

	timer_user->timer_reported = TRUE;

	return count;
}

struct file_operations visdn_timer_fops =
{
	.owner		= THIS_MODULE,
	.open		= visdn_timer_cdev_open,
	.release	= visdn_timer_cdev_release,
	.poll		= visdn_timer_cdev_poll,
	.ioctl		= visdn_timer_cdev_ioctl,
	.read		= visdn_timer_cdev_read,
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

static void visdn_timer_class_release(struct class_device *device)
{
	struct visdn_timer *timer = to_visdn_timer(device);

	visdn_debug(3, "visdn_timer_class_release()\n");

	BUG_ON(!timer->ops);

	if (timer->ops->release)
		timer->ops->release(timer);
}

static struct class visdn_timer_class = {
	.name = "visdn-timer",
	.release = visdn_timer_class_release,
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
	return print_dev_t(buf, visdn_timer_first_dev);
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

	visdn_debug(3, "visdn_timer_register(%s) (HZ=%d)\n", timer->name, HZ);

	{
	int cur_index = 0;
	struct visdn_timer *timer_tmp;

retry:
	if (cur_index >= 256) {
		err = -ENOSPC;
		goto err_index_not_found;
	}

	spin_lock(&visdn_timer_list_lock);
	list_for_each_entry(timer_tmp, &visdn_timer_list, timer_list_node) {
		if (timer_tmp->index == cur_index) {
			spin_unlock(&visdn_timer_list_lock);
			cur_index++;
			goto retry;
		}
	}
	timer->index = cur_index;
	spin_unlock(&visdn_timer_list_lock);
	}

	memset(&timer->control_class_dev, 0, sizeof(timer->control_class_dev));
	timer->control_class_dev.class = &visdn_timer_class;
	timer->control_class_dev.class_data = timer;
#ifdef HAVE_CLASS_DEV_DEVT
	timer->control_class_dev.devt = visdn_timer_first_dev;
#endif

	snprintf(timer->control_class_dev.class_id,
		sizeof(timer->control_class_dev.class_id),
		"%s", timer->name);

	err = class_device_register(&timer->control_class_dev);
	if (err < 0)
		goto err_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&timer->control_class_dev,
		&class_device_attr_dev);
#endif

	err = class_device_create_file(
			&timer->control_class_dev,
			&class_device_attr_timer_freq);
	if (err < 0)
		goto err_class_device_create_file;

#define TIMER_FREQUENCY 250

	INIT_LIST_HEAD(&timer->users_list);
	spin_lock_init(&timer->users_list_lock);
	timer->main_divider =
		max(timer->natural_frequency / TIMER_FREQUENCY, 1);
	timer->poll_divider = 5;
	timer->poll_count = 0;

	spin_lock(&visdn_timer_list_lock);
	list_add_tail(&timer->timer_list_node, &visdn_timer_list);
	spin_unlock(&visdn_timer_list_lock);

	if (timer->ops->open)
		timer->ops->open(timer);

	return 0;

err_class_device_create_file:
	class_device_del(&timer->control_class_dev);
err_class_device_register:
err_index_not_found:

	return err;
}
EXPORT_SYMBOL(visdn_timer_register);

void visdn_timer_unregister(
	struct visdn_timer *timer)
{
	visdn_debug(3, "visdn_timer_unregister()\n");

	spin_lock(&visdn_timer_list_lock);
	list_del_init(&timer->timer_list_node);
	spin_unlock(&visdn_timer_list_lock);

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&timer->control_class_dev,
	        &class_device_attr_dev);
#endif

	class_device_del(&timer->control_class_dev);
}
EXPORT_SYMBOL(visdn_timer_unregister);

int visdn_timer_modinit(void)
{
	int err;

	spin_lock_init(&visdn_timer_list_lock);

	err = class_register(&visdn_timer_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&visdn_timer_first_dev, 0, 256, "visdn-timer");
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&visdn_timer_cdev, &visdn_timer_fops);
	visdn_timer_cdev.owner = THIS_MODULE;

	err = cdev_add(&visdn_timer_cdev, visdn_timer_first_dev, 256);
	if (err < 0)
		goto err_cdev_add;

	return 0;

	cdev_del(&visdn_timer_cdev);
err_cdev_add:
	unregister_chrdev_region(visdn_timer_first_dev, 256);
err_register_chrdev:
	class_unregister(&visdn_timer_class);
err_class_register:

	return err;

}

void visdn_timer_modexit(void)
{
	cdev_del(&visdn_timer_cdev);

	unregister_chrdev_region(visdn_timer_first_dev, 256);

	class_unregister(&visdn_timer_class);
}

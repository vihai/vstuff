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

#include "visdn-timer.h"

static wait_queue_head_t timerwait;
static struct timer_list timer;
static int timer_fired = 0;
static dev_t visdn_first_dev;
static struct cdev visdn_cdev;


static void visdn_timer(unsigned long data)
{
	timer_fired = 1;
	wake_up(&timerwait);

	timer.expires = jiffies + HZ/100;

	add_timer(&timer);
}

int visdn_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

int visdn_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t visdn_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	}

	return -EINVAL;
}


static unsigned int visdn_poll(
	struct file *file,
	poll_table *wait)
{
	poll_wait(file, &timerwait, wait);

	if (timer_fired) {
		timer_fired = 0;
		return POLLIN | POLLRDNORM;
	} else {
		return 0;
	}
}

struct file_operations visdn_fops =
{
	.owner		= THIS_MODULE,
//	.ioctl		= visdn_ioctl,
	.open		= visdn_open,
	.release	= visdn_release,
	.poll		= visdn_poll,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

static int __init visdn_init_module(void)
{
	int err;

	cdev_init(&visdn_cdev, &visdn_fops);
	visdn_cdev.owner = THIS_MODULE;

	err = alloc_chrdev_region(&visdn_first_dev, 0, 1, visdn_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	err = cdev_add(&visdn_cdev, visdn_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	init_waitqueue_head(&timerwait);
	init_timer(&timer);

	timer.expires = jiffies + HZ/100;
	timer.function = visdn_timer;
	timer.data = 0;

	add_timer(&timer);

	return 0;

	cdev_del(&visdn_cdev);
err_cdev_add:
	unregister_chrdev_region(visdn_first_dev, 1);
err_register_chrdev:

	return err;
}

module_init(visdn_init_module);

static void __exit visdn_module_exit(void)
{
	del_timer_sync(&timer);

	cdev_del(&visdn_cdev);
	unregister_chrdev_region(visdn_first_dev, 1);

	printk(KERN_INFO visdn_DRIVER_DESCR " unloaded\n");
}

module_exit(visdn_module_exit);

MODULE_DESCRIPTION(visdn_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

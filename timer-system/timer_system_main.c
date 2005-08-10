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

#include <visdn.h>

#include "timer_system.h"

static wait_queue_head_t timerwait;
static struct timer_list timer;
static int timer_fired = 0;
static struct visdn_timer visdn_timer;

static void visdn_timer_func(unsigned long data)
{
	timer_fired = 1;
	wake_up(&timerwait);

	timer.expires = jiffies + HZ/100;

	add_timer(&timer);
}

static unsigned int visdn_poll(
	struct visdn_timer *visdn_timer,
	poll_table *wait)
{
	poll_wait(visdn_timer->file, &timerwait, wait);

	if (timer_fired) {
		timer_fired = 0;
		return POLLIN | POLLRDNORM;
	} else {
		return 0;
	}
}

static struct visdn_timer_ops visdn_timer_ops = {
	.poll	= visdn_poll,
};

/******************************************
 * Module stuff
 ******************************************/

static int __init visdn_init_module(void)
{
	int err;

	visdn_timer_init(&visdn_timer, &visdn_timer_ops);
	err = visdn_timer_register(&visdn_timer, "system");
	if (err < 0)
		goto err_visdn_timer_register;

	init_waitqueue_head(&timerwait);
	init_timer(&timer);

	timer.expires = jiffies + HZ/100;
	timer.function = visdn_timer_func;
	timer.data = 0;

	add_timer(&timer);

	return 0;

	visdn_timer_unregister(&visdn_timer);
err_visdn_timer_register:

	return err;
}

module_init(visdn_init_module);

static void __exit visdn_module_exit(void)
{
	del_timer_sync(&timer);

	visdn_timer_unregister(&visdn_timer);

	printk(KERN_INFO visdn_MODULE_DESCR " unloaded\n");
}

module_exit(visdn_module_exit);

MODULE_DESCRIPTION(visdn_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

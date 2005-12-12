/*
 * vISDN timer implementation based on the system timer
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

#include <linux/visdn/timer.h>

#include "timer-system.h"

static struct timer_list timer;
static struct visdn_timer vts_timer;

static void vts_timer_func(unsigned long data)
{
	timer.expires += vts_timer.main_divider;

	visdn_timer_tick(&vts_timer);

	add_timer(&timer);
}

static int vts_timer_open(
	struct visdn_timer *visdn_timer)
{
	printk(KERN_INFO "vts_timer_open()\n");

	timer.expires = jiffies;
	timer.function = vts_timer_func;
	timer.data = 0;

	add_timer(&timer);

	return 0;
}

static int vts_timer_close(
	struct visdn_timer *visdn_timer)
{
	printk(KERN_INFO "vts_timer_close()\n");

	return 0;
}

static struct visdn_timer_ops vts_timer_ops = {
	.owner			= THIS_MODULE,
	.open			= vts_timer_open,
	.close			= vts_timer_close,
};

/******************************************
 * Module stuff
 ******************************************/

static int __init vts_init_module(void)
{
	int err;

	init_timer(&timer);

	visdn_timer_init(&vts_timer);

	vts_timer.ops = &vts_timer_ops;
	strncpy(vts_timer.name, "system", 6);
	vts_timer.natural_frequency = HZ;

	err = visdn_timer_register(&vts_timer);
	if (err < 0)
		goto err_visdn_timer_register;

	return 0;

	visdn_timer_unregister(&vts_timer);
err_visdn_timer_register:

	return err;
}
module_init(vts_init_module);

static void __exit vts_module_exit(void)
{
	del_timer_sync(&timer);

	visdn_timer_unregister(&vts_timer);

	printk(KERN_INFO vts_MODULE_DESCR " unloaded\n");
}

module_exit(vts_module_exit);

MODULE_DESCRIPTION(vts_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

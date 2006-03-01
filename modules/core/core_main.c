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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include <kernel_config.h>

#include "core.h"
#include "visdn_mod.h"
#include "cxc.h"
#include "chan.h"
#include "port.h"
#include "timer.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

dev_t visdn_first_dev;
EXPORT_SYMBOL(visdn_first_dev);

static void visdn_release(struct class_device *cd)
{
}

struct class visdn_system_class = {
	.name = "visdn",
	.release = visdn_release,
};
EXPORT_SYMBOL(visdn_system_class);

static void visdn_system_device_release(struct device *cd)
{
}

struct device visdn_system_device;
EXPORT_SYMBOL(visdn_system_device);

static struct notifier_block *visdn_notify_chain;

int visdn_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&visdn_notify_chain, nb);
}
EXPORT_SYMBOL(visdn_register_notifier);

int visdn_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&visdn_notify_chain, nb);
}
EXPORT_SYMBOL(visdn_unregister_notifier);

int visdn_call_notifiers(unsigned long val, void *v)
{
	return notifier_call_chain(&visdn_notify_chain, val, v);
}
EXPORT_SYMBOL(visdn_call_notifiers);

static int __init visdn_init_module(void)
{
	int err;

	visdn_msg(KERN_INFO, "loading\n");

	err = alloc_chrdev_region(&visdn_first_dev, 0, 2, visdn_MODULE_NAME);
	if (err < 0)
		goto err_alloc_chrdev_region;

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

	err = visdn_cxc_modinit();
	if (err < 0)
		goto err_cxc_modinit;

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
	visdn_cxc_modexit();
err_cxc_modinit:
	class_unregister(&visdn_system_class);
err_class_register:
	device_unregister(&visdn_system_device);
err_system_device_register:
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
	visdn_cxc_modexit();

	class_unregister(&visdn_system_class);

	device_unregister(&visdn_system_device);

	unregister_chrdev_region(visdn_first_dev, 2);

	visdn_msg(KERN_INFO, "unloaded\n");
}
module_exit(visdn_module_exit);

MODULE_DESCRIPTION(visdn_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

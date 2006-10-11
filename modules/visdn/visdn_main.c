/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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
#include <linux/device.h>
#include <linux/notifier.h>

#include <kernel_config.h>

#include <linux/lapd.h>

#include "visdn.h"
#include "visdn_priv.h"
#include "port.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

dev_t visdn_first_dev;
EXPORT_SYMBOL(visdn_first_dev);

struct sk_buff *visdn_alloc_skb(unsigned int length)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(length + sizeof(struct lapd_prim_hdr));
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct lapd_prim_hdr));

	return skb;
}
EXPORT_SYMBOL(visdn_alloc_skb);

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
static struct raw_notifier_head visdn_notify_chain;

int visdn_register_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&visdn_notify_chain, nb);
}
EXPORT_SYMBOL(visdn_register_notifier);

int visdn_unregister_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&visdn_notify_chain, nb);
}
EXPORT_SYMBOL(visdn_unregister_notifier);

int visdn_call_notifiers(unsigned long val, void *v)
{
	return raw_notifier_call_chain(&visdn_notify_chain, val, v);
}
EXPORT_SYMBOL(visdn_call_notifiers);

#else

static struct notifier_head *visdn_notify_chain;

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
#endif

const char *visdn_event_to_text(enum visdn_event event)
{
	switch(event) {
	case VISDN_EVENT_PORT_REGISTERED:
		return "PORT_REGISTERED";
	case VISDN_EVENT_PORT_UNREGISTERED:
		return "PORT_REGISTERED";
	case VISDN_EVENT_PORT_ENABLED:
		return "PORT_ENABLED";
	case VISDN_EVENT_PORT_DISABLED:
		return "PORT_DISABLED";
	case VISDN_EVENT_PORT_CONNECTED:
		return "PORT_CONNECTED";
	case VISDN_EVENT_PORT_DISCONNECTED:
		return "PORT_DISCONNECTED";
	case VISDN_EVENT_PORT_ACTIVATED:
		return "PORT_ACTIVATED";
	case VISDN_EVENT_PORT_DEACTIVATED:
		return "PORT_DEACTIVATED";
	case VISDN_EVENT_PORT_ERROR_INDICATION_0:
		return "ERROR_INDICATION(0)";
	case VISDN_EVENT_PORT_ERROR_INDICATION_1:
		return "ERROR_INDICATION(1)";
	case VISDN_EVENT_PORT_ERROR_INDICATION_2:
		return "ERROR_INDICATION(2)";
	case VISDN_EVENT_PORT_ERROR_INDICATION_3:
		return "ERROR_INDICATION(3)";
	case VISDN_EVENT_PORT_ERROR_INDICATION_4:
		return "ERROR_INDICATION(4)";
	default:
		return "*INVALID*";
	}
}
EXPORT_SYMBOL(visdn_event_to_text);

decl_subsys(visdn, NULL, NULL);

static int __init visdn_init_module(void)
{
	int err;

	visdn_msg(KERN_INFO, "loading\n");

	err = subsystem_register(&visdn_subsys);
	if (err < 0)
		goto err_subsystem_register;

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

	err = visdn_port_modinit();
	if (err < 0)
		goto err_port_modinit;

	return 0;

	visdn_port_modexit();
err_port_modinit:
	class_unregister(&visdn_system_class);
err_class_register:
	device_unregister(&visdn_system_device);
err_system_device_register:
	unregister_chrdev_region(visdn_first_dev, 2);
err_alloc_chrdev_region:
	subsystem_unregister(&visdn_subsys);
err_subsystem_register:

	return err;
}

module_init(visdn_init_module);

static void __exit visdn_module_exit(void)
{
	visdn_port_modexit();

	class_unregister(&visdn_system_class);

	device_unregister(&visdn_system_device);

	unregister_chrdev_region(visdn_first_dev, 2);

	subsystem_unregister(&visdn_subsys);

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

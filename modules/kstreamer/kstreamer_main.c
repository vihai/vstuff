/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
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
#include <linux/notifier.h>

#include <kernel_config.h>

#include <linux/lapd.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "channel.h"
#include "duplex.h"
#include "pipeline.h"
#include "netlink.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

DECLARE_RWSEM(kstreamer_subsys_rwsem);
decl_subsys(kstreamer, NULL, NULL);

static void ks_system_device_release(struct device *cd)
{
}

struct device ks_system_device;
EXPORT_SYMBOL(ks_system_device);

void ks_kobj_waitref(struct kobject *kobj)
{
	if (atomic_read(&kobj->kref.refcount) > 1) {
		msleep(50);

		while(atomic_read(&kobj->kref.refcount) > 1) {
			ks_msg(KERN_WARNING,
				"Waiting for '%s' refcnt to become 1"
				" (now %d)\n",
				kobject_name(kobj),
				atomic_read(&kobj->kref.refcount));

			msleep(1000);
		}
	}
}

static void ks_system_class_release(struct class_device *cd)
{
}

struct class ks_system_class = {
	.name = "kstreamer",
	.release = ks_system_class_release,
};
EXPORT_SYMBOL(ks_system_class);

static int __init ks_init_module(void)
{
	int err;

	ks_msg(KERN_INFO, "loading\n");

	err = class_register(&ks_system_class);
	if (err < 0)
		goto err_class_register;

	err = subsystem_register(&kstreamer_subsys);
	if (err < 0)
		goto err_subsystem_register;

	ks_system_device.bus = NULL;
	ks_system_device.parent = NULL;
	ks_system_device.driver_data = NULL;
	ks_system_device.release = ks_system_device_release;

	snprintf(ks_system_device.bus_id,
		sizeof(ks_system_device.bus_id),
		"ks-system");

	err = device_register(&ks_system_device);
	if (err < 0)
		goto err_system_device_register;

	err = ks_node_modinit();
	if (err < 0)
		goto err_node_modinit;

	err = ks_chan_modinit();
	if (err < 0)
		goto err_chan_modinit;

	err = ks_pipeline_modinit();
	if (err < 0)
		goto err_pipeline_modinit;

	err = ks_duplex_modinit();
	if (err < 0)
		goto err_duplex_modinit;

	err = ks_netlink_modinit();
	if (err < 0)
		goto err_netlink_modinit;

	return 0;

	ks_netlink_modexit();
err_netlink_modinit:
	ks_duplex_modexit();
err_duplex_modinit:
	ks_pipeline_modexit();
err_pipeline_modinit:
	ks_chan_modexit();
err_chan_modinit:
	ks_node_modexit();
err_node_modinit:
	device_unregister(&ks_system_device);
err_system_device_register:
	subsystem_unregister(&kstreamer_subsys);
err_subsystem_register:
	class_unregister(&ks_system_class);
err_class_register:

	return err;
}

module_init(ks_init_module);

static void __exit ks_module_exit(void)
{
	ks_netlink_modexit();
	ks_duplex_modexit();
	ks_pipeline_modexit();
	ks_chan_modexit();
	ks_node_modexit();

	device_unregister(&ks_system_device);

	subsystem_unregister(&kstreamer_subsys);

	class_unregister(&ks_system_class);

	ks_msg(KERN_INFO, "unloaded\n");
}
module_exit(ks_module_exit);

MODULE_DESCRIPTION(ks_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

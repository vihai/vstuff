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
#include <linux/string.h>

#include <kernel_config.h>

#include "visdn.h"
#include "visdn_mod.h"
#include "port.h"

struct hlist_head visdn_port_index_hash[1 << VISDN_PORT_HASHBITS];

static ssize_t hfc_show_enabled(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct visdn_port *port = to_visdn_port(device);

	int len;
	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	len = snprintf(buf, PAGE_SIZE, "%d\n", port->enabled ? 1 : 0);

	visdn_port_unlock(port);

	return len;
}

static ssize_t hfc_store_enabled(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct visdn_port *port = to_visdn_port(device);

	int enabled;
	if (sscanf(buf, "%d", &enabled) < 1)
		return -EINVAL;

	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	if (enabled && !port->enabled) {
		if (port->ops->enable)
			port->ops->enable(port);

		port->enabled = enabled;
	} else if (!enabled && port->enabled) {
		if (port->ops->disable)
			port->ops->disable(port);

		port->enabled = enabled;
	}

	visdn_port_unlock(port);

	return count;
}

static DEVICE_ATTR(enabled, S_IRUGO | S_IWUSR,
		hfc_show_enabled,
		hfc_store_enabled);


static ssize_t visdn_port_show_port_name(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct visdn_port *port = to_visdn_port(device);

	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%s\n",
				port->port_name);

	visdn_port_unlock(port);

	return len;
}

static DEVICE_ATTR(port_id, S_IRUGO | S_IWUSR,
		visdn_port_show_port_name,
		NULL);

static void visdn_port_release(struct device *device)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_port_release()\n");

	struct visdn_port *port = to_visdn_port(device);

	BUG_ON(!port->ops);

	if (port->ops->release)
		port->ops->release(port);
}

struct visdn_port *visdn_port_alloc(void)
{
	struct visdn_port *port;

	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return NULL;

	memset(port, 0x00, sizeof(*port));

	return port;
}
EXPORT_SYMBOL(visdn_port_alloc);

void visdn_port_init(
	struct visdn_port *port,
	struct visdn_port_ops *ops)
{
	BUG_ON(!port);
	BUG_ON(!ops);
	BUG_ON(!ops->owner);

	memset(port, 0x00, sizeof(*port));
	port->ops = ops;

	init_MUTEX(&port->sem);
}
EXPORT_SYMBOL(visdn_port_init);

static inline struct hlist_head *visdn_port_index_get_hash(int index)
{
	return &visdn_port_index_hash[index & ((1 << VISDN_PORT_HASHBITS) - 1)];
}

struct visdn_port *__visdn_port_get_by_index(int index)
{
	struct hlist_node *t;
	struct visdn_port *visdn_port;

	hlist_for_each_entry(visdn_port, t, visdn_port_index_get_hash(index),
			index_hlist) {
		if (visdn_port->index == index)
			return visdn_port;
	}

	return NULL;
}

static int visdn_port_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__visdn_port_get_by_index(index))
			return index;
	}
}

int visdn_port_register(
	struct visdn_port *port,
	const char *global_name,
	const char *local_name,
	struct device *parent_device)
{
	int err;

	BUG_ON(!port);
	BUG_ON(!local_name);
	BUG_ON(!global_name);

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_port_register() called\n");

	port->index = visdn_port_new_index();

	port->device.parent = parent_device;
	port->device.bus = NULL;
	port->device.driver_data = NULL;
	port->device.release = visdn_port_release;

	if (strchr(global_name, '%'))
		snprintf(port->device.bus_id,
			sizeof(port->device.bus_id),
			global_name,
			port->index);
	else
		strlcpy(port->device.bus_id,
			global_name,
			sizeof(port->device.bus_id));

	snprintf(port->port_name, sizeof(port->port_name),
		"%s", local_name);

	if (port->device.parent) {
		port->device.driver = parent_device->driver;
	}

	err = device_register(&port->device);
	if (err < 0)
		goto err_device_register;

	err = device_create_file(
			&port->device,
			&dev_attr_enabled);
	if (err < 0)
		goto err_device_create_file_enabled;

	err = device_create_file(
			&port->device,
			&dev_attr_port_id);
	if (err < 0)
		goto err_device_create_file_port_id;

	if (port->device.parent) { // FIXME
		sysfs_create_link(
			&port->device.parent->kobj,
			&port->device.kobj,
			(char *)local_name);
			// should sysfs_create_link(...., const char *name) ?
	}

	return 0;

	device_remove_file(
		&port->device,
		&dev_attr_port_id);
err_device_create_file_port_id:
	device_remove_file(
		&port->device,
		&dev_attr_enabled);
err_device_create_file_enabled:
	device_unregister(&port->device);
err_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_port_register);

void visdn_port_unregister(
	struct visdn_port *port)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_port_unregister called\n");

	device_remove_file(
		&port->device,
		&dev_attr_port_id);
	device_remove_file(
		&port->device,
		&dev_attr_enabled);

	if (port->device.parent) { // FIXME
		sysfs_remove_link(
			&port->device.parent->kobj,
			port->port_name);
	}

	device_unregister(&port->device);
}
EXPORT_SYMBOL(visdn_port_unregister);

int visdn_port_modinit(void)
{
	int i;

	for (i=0; i< ARRAY_SIZE(visdn_port_index_hash); i++) {
		INIT_HLIST_HEAD(&visdn_port_index_hash[i]);
	}

	return 0;
}

void visdn_port_modexit(void)
{
}

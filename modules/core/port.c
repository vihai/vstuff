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

#include "core.h"
#include "visdn_mod.h"
#include "port.h"

static ssize_t visdn_port_show_enabled(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	int len;
	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	len = snprintf(buf, PAGE_SIZE, "%d\n", port->enabled ? 1 : 0);

	visdn_port_unlock(port);

	return len;
}

static ssize_t visdn_port_store_enabled(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	int enabled;
	if (sscanf(buf, "%d", &enabled) < 1)
		return -EINVAL;

	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	if (enabled && !port->enabled) {
		port->enabled = enabled;

		if (port->ops->enable)
			port->ops->enable(port);
	} else if (!enabled && port->enabled) {
		port->enabled = enabled;

		if (port->ops->disable)
			port->ops->disable(port);
	}

	visdn_port_unlock(port);

	return count;
}

static VISDN_PORT_ATTR(enabled, S_IRUGO | S_IWUSR,
		visdn_port_show_enabled,
		visdn_port_store_enabled);

static ssize_t visdn_port_show_port_name(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	int len;

	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;

	len = snprintf(buf, PAGE_SIZE, "%s\n",
				port->name);

	visdn_port_unlock(port);

	return len;
}

static VISDN_PORT_ATTR(port_id, S_IRUGO | S_IWUSR,
		visdn_port_show_port_name,
		NULL);

static struct attribute *visdn_port_default_attrs[] =
{
	&visdn_port_attr_enabled.attr,
	&visdn_port_attr_port_id.attr,
	NULL,
};

void visdn_port_init(struct visdn_port *port)
{
	BUG_ON(!port);

	memset(port, 0x00, sizeof(*port));

	kobject_init(&port->kobj);
	INIT_LIST_HEAD(&port->channels);

	init_MUTEX(&port->sem);
}
EXPORT_SYMBOL(visdn_port_init);

static struct kobj_type ktype_visdn_port;

int visdn_port_register(struct visdn_port *port)
{
	int err;

	BUG_ON(!port);
	BUG_ON(!port->ops);
	BUG_ON(!port->ops->owner);

	visdn_debug(3, "visdn_port_register()\n");

	if (!visdn_port_get(port)) {
		err  = -EINVAL;
		goto err_port_get;
	}

	if (port->device) {
		if (!get_device(port->device)) {
			err = -EINVAL;
			goto err_device_get;
		}
	}

	kobject_set_name(&port->kobj, "%s", port->name);
	port->kobj.parent = &port->device->kobj;
	port->kobj.ktype = &ktype_visdn_port;

	err = kobject_add(&port->kobj);
	if (err < 0)
		goto err_kobject_add;

	return 0;

	kobject_del(&port->kobj);
err_kobject_add:
	if (port->device)
		put_device(port->device);
err_device_get:
	visdn_port_put(port);
err_port_get:

	return err;
}
EXPORT_SYMBOL(visdn_port_register);

void visdn_port_unregister(
	struct visdn_port *port)
{
	visdn_debug(3, "visdn_port_unregister called\n");

	kobject_del(&port->kobj);

	if (port->device)
		put_device(port->device);
}
EXPORT_SYMBOL(visdn_port_unregister);

int visdn_port_create_file(
	struct visdn_port *port,
	struct visdn_port_attribute *attr)
{
	int err = 0;

	if (visdn_port_get(port)) {
		err = sysfs_create_file(&port->kobj, &attr->attr);
		visdn_port_put(port);
	}

	return err;
}
EXPORT_SYMBOL(visdn_port_create_file);

void visdn_port_remove_file(
	struct visdn_port *port,
	struct visdn_port_attribute *attr)
{
	if (visdn_port_get(port)) {
		sysfs_remove_file(&port->kobj, &attr->attr);
		visdn_port_put(port);
	}
}
EXPORT_SYMBOL(visdn_port_remove_file);

#define to_visdn_port_attr(_attr) \
	container_of(_attr, struct visdn_port_attribute, attr)

static ssize_t visdn_port_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_port_attribute *visdn_port_attr =
					to_visdn_port_attr(attr);
	struct visdn_port *visdn_port = to_visdn_port(kobj);
	ssize_t err;

	if (visdn_port_attr->show)
		err = visdn_port_attr->show(visdn_port, visdn_port_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_port_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_port_attribute *visdn_port_attr =
					to_visdn_port_attr(attr);
	struct visdn_port *visdn_port = to_visdn_port(kobj);
	ssize_t err;

	if (visdn_port_attr->store)
		err = visdn_port_attr->store(visdn_port, visdn_port_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_port_sysfs_ops = {
	.show   = visdn_port_attr_show,
	.store  = visdn_port_attr_store,
};

static void visdn_port_release(struct kobject *kobj)
{
	struct visdn_port *port = to_visdn_port(kobj);

	visdn_debug(3, "visdn_port_release()\n");

	if (port->ops->release)
		port->ops->release(port);
	else {
		visdn_msg(KERN_ERR, "vISDN port '%s' does not have a release()"
			" function, it is broken and must be fixed.\n",
			port->name);
		WARN_ON(1);
	}
}

static struct kobj_type ktype_visdn_port = {
	.release	= visdn_port_release,
	.sysfs_ops	= &visdn_port_sysfs_ops,
	.default_attrs	= visdn_port_default_attrs,
};

int visdn_port_modinit(void)
{
	return 0;
}

void visdn_port_modexit(void)
{
}

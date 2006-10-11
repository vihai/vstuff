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
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/string.h>

#include <kernel_config.h>

#include "visdn.h"
#include "visdn_priv.h"
#include "port.h"

static ssize_t visdn_port_show_enabled(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	int len;
/*	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;*/

	len = snprintf(buf, PAGE_SIZE, "%d\n", port->enabled ? 1 : 0);

//	visdn_port_unlock(port);

	return len;
}

/*---------------------------------------------------------------------------*/

static ssize_t visdn_port_store_enabled(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	int enabled;
	if (sscanf(buf, "%d", &enabled) < 1)
		return -EINVAL;

/*	if (visdn_port_lock_interruptible(port))
		return -ERESTARTSYS;*/

	if (enabled && !port->enabled) {

		port->enabled = TRUE;

		if (port->ops->enable)
			port->ops->enable(port);

		visdn_call_notifiers(VISDN_EVENT_PORT_ENABLED, port);

	} else if (!enabled && port->enabled) {

		port->enabled = FALSE;

		if (port->ops->disable)
			port->ops->disable(port);

		visdn_call_notifiers(VISDN_EVENT_PORT_DISABLED, port);
	}

//	visdn_port_unlock(port);

	return count;
}

static VISDN_PORT_ATTR(enabled, S_IRUGO | S_IWUSR,
		visdn_port_show_enabled,
		visdn_port_store_enabled);

static struct attribute *visdn_port_default_attrs[] =
{
	&visdn_port_attr_enabled.attr,
	NULL,
};

void visdn_port_connected(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_CONNECTED, port);
}
EXPORT_SYMBOL(visdn_port_connected);

void visdn_port_disconnected(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_DISCONNECTED, port);
}
EXPORT_SYMBOL(visdn_port_disconnected);

void visdn_port_enabled(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_ENABLED, port);
}
EXPORT_SYMBOL(visdn_port_enabled);

void visdn_port_disabled(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_DISABLED, port);
}
EXPORT_SYMBOL(visdn_port_disabled);

void visdn_port_activated(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_ACTIVATED, port);
}
EXPORT_SYMBOL(visdn_port_activated);

void visdn_port_deactivated(struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_DEACTIVATED, port);
}
EXPORT_SYMBOL(visdn_port_deactivated);

void visdn_port_error_indication(struct visdn_port *port, int code)
{
	/* We cannot pass another parameter to visdn_call_notifiers() besid
	 * the port name, so, we use multiple events to convey the error code
	 */

	switch(code) {
	case 0:
		visdn_call_notifiers(
			VISDN_EVENT_PORT_ERROR_INDICATION_0, port);
	break;

	case 1:
		visdn_call_notifiers(
			VISDN_EVENT_PORT_ERROR_INDICATION_1, port);
	break;

	case 2:
		visdn_call_notifiers(
			VISDN_EVENT_PORT_ERROR_INDICATION_2, port);
	break;


	case 3:
		visdn_call_notifiers(
			VISDN_EVENT_PORT_ERROR_INDICATION_3, port);
	break;


	case 4:
		visdn_call_notifiers(
			VISDN_EVENT_PORT_ERROR_INDICATION_4, port);
	break;

	default:
		WARN_ON(1);
	}
}
EXPORT_SYMBOL(visdn_port_error_indication);

void visdn_port_activate(struct visdn_port *port)
{
	if (port && port->ops && port->ops->activate)
		port->ops->activate(port);
}
EXPORT_SYMBOL(visdn_port_activate);

void visdn_port_deactivate(struct visdn_port *port)
{
	if (port->ops->deactivate)
		port->ops->deactivate(port);
}
EXPORT_SYMBOL(visdn_port_deactivate);

void visdn_port_init(
	struct visdn_port *port,
	struct visdn_port_ops *ops,
	const char *name,
	struct kobject *parent)
{
	BUG_ON(!port);
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!parent);

	memset(port, 0, sizeof(*port));

	kobject_init(&port->kobj);
	kobject_set_name(&port->kobj, "%s", name);
	kobj_set_kset_s(port, visdn_ports_subsys);
	port->kobj.parent = parent;

	port->ops = ops;

//	INIT_LIST_HEAD(&port->channels);

//	init_MUTEX(&port->sem);
}
EXPORT_SYMBOL(visdn_port_init);

int visdn_port_register(struct visdn_port *port)
{
	int err;

	BUG_ON(!port);

	visdn_debug(3, "visdn_port_register()\n");

	err = kobject_add(&port->kobj);
	if (err < 0)
		goto err_kobject_add;

/*	if (port->device) {
		err = sysfs_create_link(
			&port->kobj,
			&port->device->kobj,
			"device");
		if (err < 0)
			goto err_create_link_device;

		err = sysfs_create_link(
			&port->device->kobj,
			&port->kobj,
			port->kobj.name);
		if (err < 0)
			goto err_create_link_device_parent;
	}*/

	visdn_call_notifiers(VISDN_EVENT_PORT_REGISTERED, port);

	return 0;

/*	if (port->device)
		sysfs_remove_link(&port->device->kobj, port->kobj.name);
err_create_link_device_parent:
	if (port->device)
		sysfs_remove_link(&port->kobj, "device");
err_create_link_device:*/
	kobject_del(&port->kobj);
err_kobject_add:

	return err;
}
EXPORT_SYMBOL(visdn_port_register);

void visdn_port_unregister(
	struct visdn_port *port)
{
	visdn_call_notifiers(VISDN_EVENT_PORT_UNREGISTERED, port);

/*	if (port->device) {
		sysfs_remove_link(&port->device->kobj, port->kobj.name);
		sysfs_remove_link(&port->kobj, "device");
	}*/

	kobject_del(&port->kobj);
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
			port->kobj.name);
		WARN_ON(1);
	}
}

struct kobj_type visdn_port_ktype = {
	.release	= visdn_port_release,
	.sysfs_ops	= &visdn_port_sysfs_ops,
	.default_attrs	= visdn_port_default_attrs,
};
EXPORT_SYMBOL(visdn_port_ktype);

decl_subsys_name(visdn_ports, ports, &visdn_port_ktype, NULL);

int visdn_port_modinit(void)
{
	int err;

	visdn_ports_subsys.kset.kobj.parent = &visdn_subsys.kset.kobj;

	err = subsystem_register(&visdn_ports_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&visdn_ports_subsys);
err_subsystem_register:

	return err;
}

void visdn_port_modexit(void)
{
	subsystem_unregister(&visdn_ports_subsys);
}

/*
 * vISDN low-level drivers infrastructure core
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


struct list_head visdn_ports_list = LIST_HEAD_INIT(visdn_ports_list);
rwlock_t visdn_ports_list_lock = RW_LOCK_UNLOCKED;

struct visdn_port *_visdn_port_search_by_id(int id)
{
	struct visdn_port *port;
	list_for_each_entry(port, &visdn_ports_list, node) {
		if (port->id == id)
			return port;
	}

	return NULL;
}

struct visdn_port *visdn_port_get_by_id(int id)
{
	struct visdn_port *port;

	read_lock(&visdn_ports_list_lock);
	port = visdn_port_get(_visdn_port_search_by_id(id));
	read_unlock(&visdn_ports_list_lock);

	return port;
}

static int _visdn_port_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing port ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_visdn_port_search_by_id(cur_id))
			return cur_id;
	}
}

/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/

static ssize_t visdn_port_show_type(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", port->type);
}

static VISDN_PORT_ATTR(type, S_IRUGO,
		visdn_port_show_type,
		NULL);

/*---------------------------------------------------------------------------*/

static struct attribute *visdn_port_default_attrs[] =
{
	&visdn_port_attr_enabled.attr,
	&visdn_port_attr_type.attr,
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

struct visdn_port *visdn_port_create(
	struct visdn_port *port,
	struct visdn_port_ops *ops,
	const char *name,
	struct kobject *parent)
{
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!parent);

	if (!port) {
		port = kmalloc(sizeof(*port), GFP_KERNEL);
		if (!port)
			return NULL;
	}

	memset(port, 0, sizeof(*port));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	kobject_init(&port->kobj);
#else
	kobject_init(&port->kobj, &visdn_port_ktype);
#endif

	kobject_set_name(&port->kobj, "%s", name);
	port->kobj.kset = &visdn_ports_kset;
	port->kobj.parent = parent;

	port->type = "";

	port->ops = ops;

	return port;
}
EXPORT_SYMBOL(visdn_port_create);

int visdn_port_register(struct visdn_port *port)
{
	int err;
	char idtext[9];

	BUG_ON(!port);

	visdn_debug(3, "visdn_port_register()\n");

	write_lock(&visdn_ports_list_lock);
	port->id = _visdn_port_new_id();
	list_add_tail(&visdn_port_get(port)->node,
		&visdn_ports_list);
	write_unlock(&visdn_ports_list_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	err = kobject_add(&port->kobj);
	if (err < 0)
		goto err_kobject_add;

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	{
	/* In kernels < 2.6.27 the name is kfreed before being assigned again */
	char koname[64];
	strncpy(koname, kobject_name(&port->kobj), sizeof(koname));
	err = kobject_add(&port->kobj, port->kobj.parent, "%s", koname);
	}
	if (err < 0)
		goto err_kobject_add;
#else
	err = kobject_add(&port->kobj, port->kobj.parent, "%s",
					kobject_name(&port->kobj));
	if (err < 0)
		goto err_kobject_add;
#endif

	sprintf(idtext, "%08x", port->id);

	err = sysfs_create_link(
		&visdn_ports_kset.kobj,
		&port->kobj,
		idtext);

	if (err < 0)
		goto err_create_kset_link;

	visdn_call_notifiers(VISDN_EVENT_PORT_REGISTERED, port);

	return 0;

	sysfs_remove_link(&visdn_ports_kset.kobj, idtext);
err_create_kset_link:
	kobject_del(&port->kobj);
err_kobject_add:
	write_lock(&visdn_ports_list_lock);
	list_del(&port->node);
	visdn_port_put(port);
	write_unlock(&visdn_ports_list_lock);

	return err;
}
EXPORT_SYMBOL(visdn_port_register);

void visdn_port_unregister(struct visdn_port *port)
{
	char idtext[9];

	visdn_call_notifiers(VISDN_EVENT_PORT_UNREGISTERED, port);

	sprintf(idtext, "%08x", port->id);
	sysfs_remove_link(&visdn_ports_kset.kobj, idtext);

	kobject_del(&port->kobj);

	write_lock(&visdn_ports_list_lock);
	list_del(&port->node);
	visdn_port_put(port);
	write_unlock(&visdn_ports_list_lock);
}
EXPORT_SYMBOL(visdn_port_unregister);

void visdn_port_destroy(struct visdn_port *port)
{

	if (atomic_read(&port->kobj.kref.refcount) > 1) {
		msleep(50);

		while(atomic_read(&port->kobj.kref.refcount) > 1) {
			printk(KERN_WARNING
				"Waiting for '%s' refcnt to become 1 (now %d)",
				kobject_name(&port->kobj),
				atomic_read(&port->kobj.kref.refcount));

			msleep(1000);
		}
	}

	visdn_port_put(port);
}
EXPORT_SYMBOL(visdn_port_destroy);

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
	else
		kfree(port);
}

struct kobj_type visdn_port_ktype =
{
	.release	= visdn_port_release,
	.sysfs_ops	= &visdn_port_sysfs_ops,
	.default_attrs	= visdn_port_default_attrs,
};
EXPORT_SYMBOL(visdn_port_ktype);

struct kset visdn_ports_kset;

int visdn_port_modinit(void)
{
	int err;

	visdn_ports_kset.kobj.parent = &visdn_kset.kobj;

	err = kset_register(&visdn_ports_kset);
	if (err < 0)
		goto err_kset_register;

	return 0;

	kset_unregister(&visdn_ports_kset);
err_kset_register:

	return err;
}

void visdn_port_modexit(void)
{
	kset_unregister(&visdn_ports_kset);
}

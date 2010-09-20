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

#ifndef _VISDN_PORT_H
#define _VISDN_PORT_H

#ifdef __KERNEL__

#include <linux/kobject.h>
#include <linux/list.h>

extern struct kobj_type visdn_port_ktype;

extern struct rw_semaphore visdn_ports_subsys_rwsem;
extern struct kset *visdn_ports_kset;

#define to_visdn_port(class) container_of(class, struct visdn_port, kobj)

struct visdn_chan;
struct visdn_port;
struct visdn_port_ops
{
	struct module *owner;

	void (*release)(struct visdn_port *port);

	int (*enable)(struct visdn_port *port);
	int (*disable)(struct visdn_port *port);

	int (*activate)(struct visdn_port *port);
	int (*deactivate)(struct visdn_port *port);
};

struct visdn_port
{
	struct list_head node;
	struct kobject kobj;

	int id;
	const char *type;

	struct visdn_port_ops *ops;

	void *driver_data;

	int enabled;
};

struct visdn_port_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_port *port,
		struct visdn_port_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_port *port,
		struct visdn_port_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_PORT_ATTR(_name,_mode,_show,_store) \
	struct visdn_port_attribute visdn_port_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern int visdn_port_create_file(
	struct visdn_port *port,
	struct visdn_port_attribute *attr);

extern void visdn_port_remove_file(
	struct visdn_port *port,
	struct visdn_port_attribute *attr);

extern struct visdn_port *visdn_port_create(
	struct visdn_port *visdn_port,
	struct visdn_port_ops *visdn_port_ops,
	const char *name,
	struct kobject *parent);
extern void visdn_port_destroy(struct visdn_port *port);

extern int visdn_port_register(struct visdn_port *port);
extern void visdn_port_unregister(struct visdn_port *port);
extern void visdn_port_waitrelease(struct visdn_port *port);

extern int visdn_port_modinit(void);
extern void visdn_port_modexit(void);

static inline struct visdn_port *visdn_port_get(
	struct visdn_port *port)
{
	return port ? to_visdn_port(kobject_get(&port->kobj)) : NULL;
}

static inline void visdn_port_put(
	struct visdn_port *port)
{
	if (port)
		kobject_put(&port->kobj);
}

/*static inline void visdn_port_lock(
	struct visdn_port *port)
{
	down(&port->sem);
}

static inline int visdn_port_lock_interruptible(
	struct visdn_port *port)
{
	return down_interruptible(&port->sem);
}

static inline void visdn_port_unlock(
	struct visdn_port *port)
{
	up(&port->sem);
}*/

extern void visdn_port_connected(struct visdn_port *port);
extern void visdn_port_disconnected(struct visdn_port *port);
extern void visdn_port_enabled(struct visdn_port *port);
extern void visdn_port_disabled(struct visdn_port *port);
extern void visdn_port_activated(struct visdn_port *port);
extern void visdn_port_deactivated(struct visdn_port *port);
extern void visdn_port_error_indication(struct visdn_port *port, int code);

extern void visdn_port_activate(struct visdn_port *port);
extern void visdn_port_deactivate(struct visdn_port *port);

#endif

#endif

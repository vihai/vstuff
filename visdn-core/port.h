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

#ifndef _VISDN_PORT_H
#define _VISDN_PORT_H

#ifdef __KERNEL__

#define VISDN_PORT_HASHBITS 8

extern struct hlist_head visdn_port_index_hash[];

#define to_visdn_port(class) container_of(class, struct visdn_port, device)

struct visdn_port_ops
{
	struct module *owner;

	void (*release)(struct visdn_port *port);

	int (*enable)(struct visdn_port *port);
	int (*disable)(struct visdn_port *port);
};

struct visdn_port
{
	struct semaphore sem;

	struct device device;

	struct hlist_node index_hlist;
	int index;

	char port_name[BUS_ID_SIZE];

	struct visdn_port_ops *ops;

	int enabled;

	void *priv;
};

int visdn_port_modinit(void);
void visdn_port_modexit(void);

static inline void visdn_port_get(
	struct visdn_port *port)
{
	get_device(&port->device);
}

static inline void visdn_port_put(
	struct visdn_port *port)
{
	put_device(&port->device);
}

static inline void visdn_port_lock(
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
}

extern void visdn_port_init(
	struct visdn_port *visdn_port,
	struct visdn_port_ops *ops);

extern struct visdn_port *visdn_port_alloc(void);

extern int visdn_port_register(
	struct visdn_port *visdn_port,
	const char *global_name,
	const char *local_name,
	struct device *parent_device);

extern void visdn_port_unregister(
	struct visdn_port *visdn_port);

#endif

#endif

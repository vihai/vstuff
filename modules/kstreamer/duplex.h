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

#ifndef _KS_DUPLEX_H
#define _KS_DUPLEX_H

#include <kernel_config.h>

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/sysfs.h>

extern struct subsystem ks_duplexes_subsys;

/*struct ks_duplex_class
{
	const char *name;
};
*/

struct ks_duplex;
struct ks_duplex_ops
{
	struct module *owner;

	void (*release)(struct ks_duplex *duplex);
};

#define to_ks_duplex(obj) container_of(obj, struct ks_duplex, kobj)

struct ks_duplex
{
	struct kobject kobj;

	struct ks_duplex_ops *ops;
	struct ks_port *port;

//	struct ks_duplex_class *duplex_class;


//	void *driver_data;

//	struct semaphore sem;

//	unsigned long state;

//	int bitrate;

//	int write_priority;

//	struct ks_leg leg_a;
//	struct ks_leg leg_b;

//	struct ks_router_node router_node;
};

struct ks_duplex_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct ks_duplex *duplex,
		struct ks_duplex_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct ks_duplex *duplex,
		struct ks_duplex_attribute *attr,
		const char *buf,
		size_t count);
};

#define KS_DUPLEX_ATTR(_name,_mode,_show,_store) \
	struct ks_duplex_attribute ks_duplex_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern int ks_duplex_create_file(
	struct ks_duplex *duplex,
	struct ks_duplex_attribute *entry);

extern void ks_duplex_remove_file(
	struct ks_duplex *duplex,
	struct ks_duplex_attribute * attr);

//extern struct ks_duplex *ks_duplex_get_by_id(int id);

static inline struct ks_duplex *ks_duplex_get(
	struct ks_duplex *duplex)
{
	return duplex ? to_ks_duplex(kobject_get(&duplex->kobj)) : NULL;
}

static inline void ks_duplex_put(
	struct ks_duplex *duplex)
{
	if (duplex)
		kobject_put(&duplex->kobj);
}

#if 0
static inline void ks_duplex_lock(
	struct ks_duplex *duplex)
{
	down(&duplex->sem);
}

static inline int ks_duplex_lock_interruptible(
	struct ks_duplex *duplex)
{
	return down_interruptible(&duplex->sem);
}

static inline void ks_duplex_unlock(
	struct ks_duplex *duplex)
{
	up(&duplex->sem);
}
#endif

static inline int ks_duplex_refcount(
	struct ks_duplex *duplex)
{
#ifdef HAVE_KREF
	return atomic_read(&duplex->kobj.kref.refcount);
#else
	return atomic_read(&duplex->kobj.refcount);
#endif
}

#if 0
enum ks_duplex_state
{
	KS_DUPLEX_STATE_CONNECTED	= 0,
	KS_DUPLEX_STATE_OPEN		= 1,
	KS_DUPLEX_STATE_PLAYING	= 2,
};

extern int ks_duplex_lock2(
	struct ks_duplex *duplex1,
	struct ks_duplex *duplex2);

extern int ks_duplex_lock2_interruptible(
	struct ks_duplex *duplex1,
	struct ks_duplex *duplex2);
#endif

void ks_duplex_init(
	struct ks_duplex *duplex,
	struct ks_duplex_ops *ops,
	const char *name,
	struct kobject *parent);
extern int ks_duplex_register(struct ks_duplex *duplex);
extern void ks_duplex_unregister(struct ks_duplex *duplex);

#if 0
extern int ks_duplex_open(struct ks_duplex *duplex);
extern int ks_duplex_close(struct ks_duplex *duplex);
extern int ks_duplex_start(struct ks_duplex *duplex);
extern int ks_duplex_stop(struct ks_duplex *duplex);
#endif

int ks_duplex_modinit(void);
void ks_duplex_modexit(void);

#endif

#endif

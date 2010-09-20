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

#ifndef _KS_DUPLEX_H
#define _KS_DUPLEX_H

#include <kernel_config.h>

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/sysfs.h>

extern struct rw_semaphore ks_duplexes_subsys_rwsem;
extern struct kset *ks_duplexes_subsys;

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

        char workaround_name[32];
        struct kobject *workaround_parent;
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

static inline int ks_duplex_refcount(
	struct ks_duplex *duplex)
{
#ifdef HAVE_KREF
	return atomic_read(&duplex->kobj.kref.refcount);
#else
	return atomic_read(&duplex->kobj.refcount);
#endif
}

extern struct ks_duplex *ks_duplex_create(
	struct ks_duplex *duplex,
	struct ks_duplex_ops *ops,
	const char *name,
	struct kobject *parent);
extern void ks_duplex_destroy(struct ks_duplex *duplex);

extern int ks_duplex_register(struct ks_duplex *duplex);
extern void ks_duplex_unregister(struct ks_duplex *duplex);

int ks_duplex_modinit(void);
void ks_duplex_modexit(void);

#endif

#endif

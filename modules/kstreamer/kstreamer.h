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

#ifndef __KSTREAMER_H
#define __KSTREAMER_H

#ifdef __KERNEL__

#include <linux/kobject.h>
#include <linux/version.h>

extern struct class ks_system_class;

extern struct rw_semaphore kstreamer_subsys_rwsem;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
extern struct subsystem kstreamer_subsys;
#else
extern struct kset kstreamer_subsys;
#endif

extern struct device ks_system_device;

void ks_kobj_waitref(struct kobject *kobj);

#endif

#endif

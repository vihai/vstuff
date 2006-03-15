/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_PATH_H
#define _VISDN_PATH_H

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

extern struct list_head visdn_paths_list;

struct visdn_path
{
	struct kobject kobj;

	struct list_head node;

	int id;

	struct visdn_chan *ep1;
	struct visdn_chan *ep2;

	struct file *file;
};

struct visdn_path_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_path *path,
		struct visdn_path_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_path *path,
		struct visdn_path_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_PATH_ATTR(_name,_mode,_show,_store) \
	struct visdn_path_attribute visdn_path_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

int visdn_path_modinit(void);
void visdn_path_modexit(void);

#define to_visdn_path(obj) container_of(obj, struct visdn_path, kobj)

struct visdn_path *visdn_path_alloc(void);
struct visdn_path *visdn_path_get_by_id(int id);
struct visdn_path *visdn_path_get_by_endpoint(struct visdn_chan *chan);

static inline struct visdn_path *visdn_path_get(struct visdn_path *path)
{
	return path ? to_visdn_path(kobject_get(&path->kobj)) : NULL;
}

static inline void visdn_path_put(struct visdn_path *path)
{
	if (path)
		kobject_put(&path->kobj);
}

extern int visdn_path_find_lowest_mtu(struct visdn_path *path);
extern struct visdn_chan *visdn_path_get_other_endpoint(
	struct visdn_path *path,
	struct visdn_chan *chan);

extern struct visdn_path *visdn_path_connect(
	struct visdn_chan *src_chan,
	struct visdn_chan *dst_chan,
	struct file *file,
	unsigned long flags,
	int *err);

extern struct visdn_path *visdn_path_connect_by_id(
	int chan1_id,
	int chan2_id,
	struct file *file,
	unsigned long flags,
	int *err);

extern int visdn_path_disconnect(struct visdn_path *path);
extern int visdn_path_disconnect_by_id(int id);

extern int visdn_path_enable(struct visdn_path *path);
extern int visdn_path_enable_by_id(int id);

extern int visdn_path_disable(struct visdn_path *path);
extern int visdn_path_disable_by_id(int id);
#endif

#endif

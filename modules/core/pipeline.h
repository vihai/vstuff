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

#ifndef _VISDN_PIPELINE_H
#define _VISDN_PIPELINE_H

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

extern struct list_head visdn_pipelines_list;

struct visdn_pipeline
{
	struct kobject kobj;

	struct list_head node;

	int id;

	struct visdn_chan *ep1;
	struct visdn_chan *ep2;

	struct file *file;
};

struct visdn_pipeline_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_pipeline *pipeline,
		struct visdn_pipeline_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_pipeline *pipeline,
		struct visdn_pipeline_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_PIPELINE_ATTR(_name,_mode,_show,_store) \
	struct visdn_pipeline_attribute visdn_pipeline_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

int visdn_pipeline_modinit(void);
void visdn_pipeline_modexit(void);

#define to_visdn_pipeline(obj) container_of(obj, struct visdn_pipeline, kobj)

struct visdn_pipeline *visdn_pipeline_alloc(void);
struct visdn_pipeline *visdn_pipeline_get_by_id(int id);
struct visdn_pipeline *visdn_pipeline_get_by_endpoint(struct visdn_chan *chan);

static inline struct visdn_pipeline *visdn_pipeline_get(struct visdn_pipeline *pipeline)
{
	return pipeline ? to_visdn_pipeline(kobject_get(&pipeline->kobj)) : NULL;
}

static inline void visdn_pipeline_put(struct visdn_pipeline *pipeline)
{
	if (pipeline)
		kobject_put(&pipeline->kobj);
}

extern int visdn_pipeline_find_lowest_mtu(struct visdn_pipeline *pipeline);
extern struct visdn_chan *visdn_pipeline_get_other_endpoint(
	struct visdn_pipeline *pipeline,
	struct visdn_chan *chan);

extern struct visdn_pipeline *visdn_pipeline_connect(
	struct visdn_chan *src_chan,
	struct visdn_chan *dst_chan,
	struct file *file,
	unsigned long flags,
	int *err);

extern struct visdn_pipeline *visdn_pipeline_connect_by_id(
	int chan1_id,
	int chan2_id,
	struct file *file,
	unsigned long flags,
	int *err);

extern int visdn_pipeline_disconnect(struct visdn_pipeline *pipeline);
extern int visdn_pipeline_disconnect_by_id(int id);

extern int visdn_pipeline_open(struct visdn_pipeline *pipeline);
extern int visdn_pipeline_open_by_id(int id);

extern int visdn_pipeline_close(struct visdn_pipeline *pipeline);
extern int visdn_pipeline_close_by_id(int id);

extern int visdn_pipeline_start(struct visdn_pipeline *pipeline);
extern int visdn_pipeline_start_by_id(int id);

extern int visdn_pipeline_stop(struct visdn_pipeline *pipeline);
extern int visdn_pipeline_stop_by_id(int id);
#endif

#endif

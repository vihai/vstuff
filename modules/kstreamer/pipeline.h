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

#ifndef _KS_PIPELINE_H
#define _KS_PIPELINE_H

#include <linux/kstreamer/netlink.h>

enum ks_pipeline_attribute_type
{
	KS_PIPELINEATTR_ID = 1,
	KS_PIPELINEATTR_PATH,
	KS_PIPELINEATTR_STATUS,
	KS_PIPELINEATTR_LINK_ID,
};

enum ks_pipeline_status
{
	KS_PIPELINE_STATUS_NULL,
	KS_PIPELINE_STATUS_CONNECTED,
	KS_PIPELINE_STATUS_OPEN,
	KS_PIPELINE_STATUS_FLOWING,
};

#ifdef __KERNEL__

#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/rwsem.h>

struct ks_pipeline
{
	struct kobject kobj;
	struct list_head node;

	struct rw_semaphore lock;

	int id;

	enum ks_pipeline_status status;

	struct list_head entries;

	struct file *file;

//	enum ks_pipeline_payload payload;
	int framing;
	int mtu;
};

struct ks_pipeline_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct ks_pipeline *pipeline,
		struct ks_pipeline_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct ks_pipeline *pipeline,
		struct ks_pipeline_attribute *attr,
		const char *buf,
		size_t count);
};

#define KS_PIPELINE_ATTR(_name,_mode,_show,_store) \
	struct ks_pipeline_attribute ks_pipeline_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

#define to_ks_pipeline(obj) container_of(obj, struct ks_pipeline, kobj)

void ks_pipeline_init(struct ks_pipeline *pipeline);
struct ks_pipeline *ks_pipeline_alloc(void);
int ks_pipeline_register(struct ks_pipeline *pipeline);
void ks_pipeline_unregister(struct ks_pipeline *pipeline);

struct ks_pipeline *ks_pipeline_get_by_id(int id);

static inline struct ks_pipeline *ks_pipeline_get(
	struct ks_pipeline *pipeline)
{
	return pipeline ?
		to_ks_pipeline(kobject_get(&pipeline->kobj)) :
		NULL;
}

static inline void ks_pipeline_put(struct ks_pipeline *pipeline)
{
	if (pipeline)
		kobject_put(&pipeline->kobj);
}

void ks_pipeline_netlink_dump(struct ks_netlink_xact *xact);

void ks_pipeline_dump(struct ks_pipeline *pipeline);
int ks_pipeline_write_to_nlmsg(
	struct ks_pipeline *pipeline,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	u32 pid, u32 seq, u16 flags);
struct ks_pipeline *ks_pipeline_create_from_nlmsg(
	struct nlmsghdr *nlh, int *err);

//int ks_pipeline_connect(struct ks_pipeline *pipeline);
//extern void ks_pipeline_disconnect(struct ks_pipeline *pipeline);
//extern int ks_pipeline_open(struct ks_pipeline *pipeline);
//extern void ks_pipeline_close(struct ks_pipeline *pipeline);
//extern int ks_pipeline_start(struct ks_pipeline *pipeline);
//extern void ks_pipeline_stop(struct ks_pipeline *pipeline);
extern int ks_pipeline_setstatus(
	struct ks_pipeline *pipeline,
	enum ks_pipeline_status status);
extern void ks_pipeline_stimulate(struct ks_pipeline *pipeline);

extern struct ks_link *ks_pipeline_first_link(
		struct ks_pipeline *pipeline);
extern struct ks_link *ks_pipeline_last_link(
		struct ks_pipeline *pipeline);
extern struct ks_link *ks_pipeline_prev(struct ks_link *link);
extern struct ks_link *ks_pipeline_next(struct ks_link *link);
extern struct ks_node *ks_pipeline_first_node(
		struct ks_pipeline *pipeline);
extern struct ks_node *ks_pipeline_last_node(
		struct ks_pipeline *pipeline);

int ks_pipeline_modinit(void);
void ks_pipeline_modexit(void);

#endif

#endif

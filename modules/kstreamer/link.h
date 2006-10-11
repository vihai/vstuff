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

#ifndef _KS_LINK_H
#define _KS_LINK_H

enum ks_link_attribute_type
{
	KS_LINKATTR_ID = 1,
	KS_LINKATTR_PATH,
	KS_LINKATTR_FROM,
	KS_LINKATTR_TO,
};

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/rcupdate.h>

#include <kernel_config.h>

#include "netlink.h"

extern struct kset ks_links_kset;
extern struct list_head ks_links_list;
extern rwlock_t ks_links_list_lock;

/*

enum ks_leg_rx_error_code
{
	KS_RX_ERROR_DROPPED,
	KS_RX_ERROR_LENGTH,
	KS_RX_ERROR_CRC,
	KS_RX_ERROR_FR_ABORT,
};

enum ks_leg_tx_error_code
{
	KS_TX_ERROR_FIFO_FULL,
};

*/

enum ks_push_frame_return_codes
{
	KS_TX_OK,
	KS_TX_BUSY,
	KS_TX_LOCKED,
};

struct ks_chan;
struct ks_link;

struct ks_link_ops
{
	struct module *owner;

	void (*release)(struct ks_link *link);

	int (*connect)(struct ks_link *link);
	void (*disconnect)(struct ks_link *link);

	int (*open)(struct ks_link *link);
	void (*close)(struct ks_link *link);

	int (*start)(struct ks_link *link);
	void (*stop)(struct ks_link *link);

	void (*stimulus)(struct ks_link *link);

	int (*get_attr_count)(struct ks_link *link);
	int (*get_attr)(struct ks_link *link, int index, u16 *type, void *buf, int *len);
	int (*set_attr)(struct ks_link *link, u16 type, void *buf, int len);
};

/*
enum ks_leg_status
{
	KS_LEG_STATUS_QUEUE_STOPPED,
};
*/

struct ks_link
{
	struct kobject kobj;
	struct list_head node;

	int id;

	struct ks_link_ops *ops;
	struct ks_duplex *duplex;

	struct ks_node *from;
	void *from_ops;

	struct ks_node *to;
//	void *to_ops;

	struct ks_pipeline *pipeline;
	struct list_head pipeline_entry;
	struct rcu_head pipeline_entry_rcu;

	void *driver_data;
};

#define to_ks_link(obj) container_of(obj, struct ks_link, kobj)

struct ks_link_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct ks_link *link,
		struct ks_link_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct ks_link *link,
		struct ks_link_attribute *attr,
		const char *buf,
		size_t count);
};

#define KS_LINK_ATTR(_name,_mode,_show,_store) \
	struct ks_link_attribute ks_link_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern int ks_link_create_file(
	struct ks_link *link,
	struct ks_link_attribute *entry);

extern void ks_link_remove_file(
	struct ks_link *chan,
	struct ks_link_attribute * attr);

/*
extern int ks_leg_frame_xmit(
	struct ks_leg *leg,
	struct sk_buff *skb);
extern void ks_leg_rx_error(
	struct ks_leg *leg,
	enum ks_leg_rx_error_code code);
extern void ks_leg_tx_error(
	struct ks_leg *leg,
	enum ks_leg_tx_error_code code);
*/
void ks_link_init(
	struct ks_link *link,
	struct ks_link_ops *ops,
	const char *name,
	struct ks_duplex *duplex,
	struct kobject *parent,
	struct ks_node *from,
	struct ks_node *to);
extern int ks_link_register(struct ks_link *link);
extern void ks_link_unregister(struct ks_link *link);

struct ks_link *ks_link_get_by_id(int id);
struct ks_link *ks_link_get_by_nlid(struct nlmsghdr *nlh);

void ks_link_netlink_dump(struct ks_netlink_xact *xact);
void ks_link_update_from_nlmsg(struct ks_link *link, struct nlmsghdr *nlh);

static inline struct ks_link *ks_link_get(struct ks_link *link)
{
	return link ? to_ks_link(kobject_get(&link->kobj)) : NULL;
}

static inline void ks_link_put(struct ks_link *link)
{
//	ks_debug(3, "ks_link_put() refcnt=%d\n",
//			atomic_read(&link->kobj.kref.refcount) - 1);

	if (link)
		kobject_put(&link->kobj);
}

/*static inline int ks_leg_queue_stopped(struct ks_leg *leg)
{
	return test_bit(KS_LEG_STATUS_QUEUE_STOPPED, &leg->status);
}*/

void ks_link_del_rcu(struct rcu_head *head);

int ks_link_modinit(void);
void ks_link_modexit(void);

#endif

#endif

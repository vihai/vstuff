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

#ifndef _VISDN_CXC_H
#define _VISDN_CXC_H

#include "chan.h"

#define VISDN_IOC_CONNECT		_IOR(0xd0, 2, unsigned int)
#define VISDN_IOC_DISCONNECT		_IOR(0xd0, 3, unsigned int)
#define VISDN_IOC_CONNECT_PATH		_IOR(0xd0, 4, unsigned int)
#define VISDN_IOC_DISCONNECT_PATH	_IOR(0xd0, 5, unsigned int)
#define VISDN_IOC_ENABLE_PATH		_IOR(0xd0, 6, unsigned int)
#define VISDN_IOC_DISABLE_PATH		_IOR(0xd0, 7, unsigned int)

#define VISDN_CONNECT_FLAG_PERMANENT	(1 << 0)

#define VISDN_CXCID_SIZE 32

struct visdn_connect
{
	int src_chan_id;
	int dst_chan_id;

	int flags;
};

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

#define VISDN_CXC_HASHBITS 8
extern struct subsystem visdn_tdm_subsys;
extern struct list_head visdn_cxc_list;

struct visdn_cxc;
struct visdn_cxc_ops
{
	struct module *owner;

	void (*release)(struct visdn_cxc *cxc);
	void (*timer_func)(struct visdn_cxc *cxc);

	int (*connect)(
		struct visdn_cxc *cxc,
		struct visdn_leg *leg1,
		struct visdn_leg *leg2);

	void (*disconnect)(
		struct visdn_cxc *cxc,
		struct visdn_leg *leg1,
		struct visdn_leg *leg2);

	int (*frame_xmit)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg,
		struct sk_buff *skb);

	void (*stop_queue)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg);
	void (*start_queue)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg);
	void (*wake_queue)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg);

	void (*rx_error)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg,
		enum visdn_leg_rx_error_code code);
	void (*tx_error)(
		struct visdn_cxc *cxc,
		struct visdn_leg *src_leg,
		enum visdn_leg_tx_error_code code);
};

struct visdn_cxc
{
	struct list_head cxc_list_node;

	struct semaphore sem;

	int id;
	const char *name;

	struct subsystem subsys;

	struct visdn_cxc_ops *ops;

	struct list_head legs;
	struct hlist_head connections_hash[1 << VISDN_CXC_HASHBITS];
	struct list_head connections_list;

	struct visdn_router_node router_node;
};

struct visdn_cxc_connection
{
	struct visdn_cxc *cxc;

	struct hlist_node hash_node;
	struct list_head list_node;
	struct rcu_head rcu;

	struct visdn_leg *src;
	struct visdn_leg *dst;

	struct file *bound_to_file;
};

struct visdn_cxc_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_cxc *cxc,
		struct visdn_cxc_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_cxc *cxc,
		struct visdn_cxc_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_CXC_ATTR(_name,_mode,_show,_store) \
	struct visdn_cxc_attribute visdn_cxc_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

struct visdn_leg *visdn_cxc_get_leg_by_id(
	struct visdn_cxc *cxc,
	int chan_id);

extern int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg);
extern void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg);

extern int visdn_cxc_connect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2,
	struct file *bound_to_file);

extern int visdn_cxc_disconnect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2);

extern struct visdn_leg *visdn_cxc_get_leg_by_src(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg);
extern int visdn_cxc_leg_connected(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg);

extern int visdn_cxc_disconnect_leg(struct visdn_leg *leg);

extern void visdn_cxc_init(struct visdn_cxc *cxc);
extern int visdn_cxc_register(struct visdn_cxc *cxc);
extern void visdn_cxc_unregister(struct visdn_cxc *cxc);

extern int visdn_cxc_create_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr);
extern void visdn_cxc_remove_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr);

extern int visdn_cxc_connect_with_id(
	int chan1_id,
	int chan2_id,
	struct file *bound_to_file);
extern int visdn_cxc_disconnect_with_id(
	int chan1_id,
	int chan2_id);

extern int visdn_cxc_modinit(void);
extern void visdn_cxc_modexit(void);

static inline struct visdn_cxc *visdn_cxc_get(
	struct visdn_cxc *cxc)
{
	return cxc ? container_of(subsys_get(&cxc->subsys),
			struct visdn_cxc, subsys) : NULL;
}

static inline void visdn_cxc_put(
	struct visdn_cxc *cxc)
{
	if (cxc)
		subsys_put(&cxc->subsys);
}

static inline struct hlist_head *visdn_cxc_get_hash(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg)
{
	return &cxc->connections_hash[
			(unsigned long)leg &
			((1 << VISDN_CXC_HASHBITS) - 1)];
}

#endif

#endif

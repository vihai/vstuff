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

#ifndef _VISDN_LEG_H
#define _VISDN_LEG_H

#include <kernel_config.h>

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/sysfs.h>

#include "router.h"

#define VISDN_LEG_FRAMING_NONE		(1 << 0)
#define VISDN_LEG_FRAMING_ASYNC		(1 << 1)
#define VISDN_LEG_FRAMING_HDLC		(1 << 2)
#define VISDN_LEG_FRAMING_MTP2		(1 << 3)

enum visdn_leg_rx_error_code
{
	VISDN_RX_ERROR_DROPPED,
	VISDN_RX_ERROR_LENGTH,
	VISDN_RX_ERROR_CRC,
	VISDN_RX_ERROR_FR_ABORT,
};

enum visdn_leg_tx_error_code
{
	VISDN_TX_ERROR_FIFO_FULL,
};

enum visdn_frame_xmit_return_codes
{
	VISDN_TX_OK,
	VISDN_TX_BUSY,
	VISDN_TX_LOCKED,
};

struct visdn_chan;
struct visdn_leg;

struct visdn_leg_ops
{
	struct module *owner;

	int (*frame_xmit)(struct visdn_leg *leg, struct sk_buff *skb);

	int (*connect)(
		struct visdn_leg *leg,
		struct visdn_leg *leg2);
	void (*disconnect)(
		struct visdn_leg *leg,
		struct visdn_leg *leg2);

	ssize_t (*read)(struct visdn_leg *leg,
		void *buf, size_t count);
	ssize_t (*write)(struct visdn_leg *leg,
		const void *buf, size_t count);

	void (*stop_queue)(struct visdn_leg *leg);
	void (*start_queue)(struct visdn_leg *leg);
	void (*wake_queue)(struct visdn_leg *leg);

	void (*rx_error)(struct visdn_leg *leg,
		enum visdn_leg_rx_error_code code);
	void (*tx_error)(struct visdn_leg *leg,
		enum visdn_leg_tx_error_code code);
};

enum visdn_leg_status
{
	VISDN_LEG_STATUS_QUEUE_STOPPED,
};

struct visdn_leg
{
	struct kobject kobj;

	struct list_head cxc_legs_node;

	struct visdn_chan *chan;
	struct visdn_cxc *cxc;

	struct visdn_leg *other_leg;

	int id;

	struct visdn_leg_ops *ops;

	int framing;
	int framing_avail;

	int mtu;

	struct visdn_router_arch router_arch;

	spinlock_t queue_stopped_lock;
	unsigned long status;
};

#define to_visdn_leg(obj) container_of(obj, struct visdn_leg, kobj)

struct visdn_leg_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_leg *leg,
		struct visdn_leg_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_leg *leg,
		struct visdn_leg_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_LEG_ATTR(_name,_mode,_show,_store) \
	struct visdn_leg_attribute visdn_leg_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern int visdn_leg_create_file(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *entry);

extern void visdn_leg_remove_file(
	struct visdn_leg *chan,
	struct visdn_leg_attribute * attr);

extern int visdn_leg_frame_xmit(
	struct visdn_leg *leg,
	struct sk_buff *skb);
extern void visdn_leg_start_queue(
	struct visdn_leg *leg);
extern void visdn_leg_stop_queue(
	struct visdn_leg *leg);
extern void visdn_leg_wake_queue(
	struct visdn_leg *leg);
extern void visdn_leg_rx_error(
	struct visdn_leg *leg,
	enum visdn_leg_rx_error_code code);
extern void visdn_leg_tx_error(
	struct visdn_leg *leg,
	enum visdn_leg_tx_error_code code);

extern void visdn_leg_init(struct visdn_leg *leg);

extern struct visdn_leg *visdn_leg_get(struct visdn_leg *leg);
extern void visdn_leg_put(struct visdn_leg *leg);

static inline int visdn_leg_queue_stopped(struct visdn_leg *leg)
{
	return test_bit(VISDN_LEG_STATUS_QUEUE_STOPPED, &leg->status);
}

#endif

#endif

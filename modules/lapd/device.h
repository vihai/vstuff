/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_DEV_H
#define _LAPD_DEV_H

#ifdef DEBUG_CODE
#define lapd_debug_dev(ld, format, arg...)			\
		printk(KERN_DEBUG				\
			"lapd: "				\
			"%s: "					\
			format,					\
			((ld->dev) && (ld)->dev->name) ? (ld)->dev->name : "",\
			## arg)
#else
#define lapd_debug_dev(ld, format, arg...) do { } while (0)
#endif

#define lapd_msg_dev(ld, lvl, format, arg...)		\
	printk(lvl "lapd: "				\
		"%s: "					\
		format,					\
		(ld)->dev->name,			\
		## arg)

enum lapd_l1_state
{
	LAPD_L1_STATE_UNAVAILABLE,
	LAPD_L1_STATE_AVAILABLE,
	LAPD_L1_STATE_ACTIVATING,
};

struct lapd_device
{
	struct net_device *dev;

	enum lapd_intf_type type;
	enum lapd_intf_role role;
	enum lapd_intf_mode mode;

	struct lapd_ntme *net_tme;
	struct lapd_sap q931;
	struct lapd_sap x25;

	enum lapd_l1_state l1_state;
	struct sk_buff_head out_queue;
	spinlock_t out_queue_lock;
};

int lapd_device_event(struct notifier_block *this,
	unsigned long event, void *ptr);

struct lapd_device *lapd_dev_get_by_name(const char *name);

static inline void lapd_dev_get(struct lapd_device *dev)
{
	dev_hold(dev->dev);
}

static inline void lapd_dev_put(struct lapd_device *dev)
{
	dev_put(dev->dev);
}

static inline struct lapd_device *to_lapd_dev(struct net_device *dev)
{
	return (struct lapd_device *)dev->atalk_ptr;
}

#endif

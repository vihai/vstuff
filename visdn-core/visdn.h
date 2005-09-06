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

#ifndef _VISDN_H
#define _VISDN_H

#include "chan.h"
#include "port.h"
#include "timer.h"

#ifdef __KERNEL__

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

extern dev_t visdn_first_dev;
extern struct device visdn_system_device;
extern struct class visdn_system_class;

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb);

static inline struct sk_buff *visdn_alloc_skb(unsigned int length)
{
	return dev_alloc_skb(length);
}

static inline void visdn_kfree_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

#endif

#endif

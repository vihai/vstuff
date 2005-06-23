/*
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _VISDN_H
#define _VISDN_H

#include "chan.h"
#include "port.h"
#include "timer.h"

extern dev_t visdn_first_dev;

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb);

static inline struct sk_buff *visdn_alloc_skb(unsigned int length)
{
	return dev_alloc_skb(length);
}

static inline void visdn_kfree_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

static inline void visdn_stop_queue(struct visdn_chan *chan)
{
	netif_stop_queue(chan->netdev);
}

static inline void visdn_start_queue(struct visdn_chan *chan)
{
	netif_start_queue(chan->netdev);
}

static inline void visdn_wake_queue(struct visdn_chan *chan)
{
	netif_wake_queue(chan->netdev);
}

#endif

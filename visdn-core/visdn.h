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

#define VISDN_SET_BEARER        SIOCDEVPRIVATE
#define VISDN_PPP_GET_CHAN      (SIOCDEVPRIVATE+1)
#define VISDN_PPP_GET_UNIT      (SIOCDEVPRIVATE+2)

#ifdef __KERNEL__

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

static inline void visdn_stop_queue(struct visdn_chan *chan)
{
	if (chan->connected_chan &&
	    chan->connected_chan->ops->stop_queue)
		chan->connected_chan->ops->stop_queue(
			chan->connected_chan);
}

static inline void visdn_start_queue(struct visdn_chan *chan)
{
	if (chan->connected_chan &&
	    chan->connected_chan->ops->start_queue)
		chan->connected_chan->ops->start_queue(
			chan->connected_chan);
}

static inline void visdn_wake_queue(struct visdn_chan *chan)
{
	if (chan->connected_chan &&
	    chan->connected_chan->ops->wake_queue)
		chan->connected_chan->ops->wake_queue(
			chan->connected_chan);
}

static inline void visdn_frame_input_error(struct visdn_chan *chan, int code)
{
	if (chan->connected_chan &&
	    chan->connected_chan->ops->frame_input_error)
		chan->connected_chan->ops->frame_input_error(
			chan->connected_chan, code);
}

/*
enum visdn_bearertype
{
        VISDN_BT_VOICE  = 1,
        VISDN_BT_PPP    = 2,
};

struct visdn_setbearer
{
        int sb_index;
        enum sb_bearertype sb_bearertype;
};
*/

#endif

#endif

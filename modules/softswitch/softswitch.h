/*
 * vISDN software crossconnector
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_SOFTSWITCH_H
#define _VISDN_SOFTSWITCH_H

#ifdef __KERNEL__

#include <linux/skbuff.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/streamframe.h>

#define vss_MODULE_NAME "softswitch"
#define vss_MODULE_PREFIX vss_MODULE_NAME ": "
#define vss_MODULE_DESCR "vISDN softswitch"

struct vss_softswitch
{
	struct ks_node ks_node;

//	u8 buf[1024];

//	unsigned long long overhead_cycles;
//	unsigned long long overhead;
//
};

struct vss_chan_ops
{
	int (*push_frame)(struct ks_chan *chan, struct sk_buff *skb);
	int (*push_raw)(struct ks_chan *chan,
			struct ks_streamframe *sb);
	int (*get_pressure)(struct ks_chan *chan);
/*

	void (*rx_error)(struct visdn_leg *leg,
		enum visdn_leg_rx_error_code code);
	void (*tx_error)(struct visdn_leg *leg,
		enum visdn_leg_tx_error_code code);
*/
};

extern struct vss_softswitch vss_softswitch;

extern int vss_chan_push_frame(struct ks_chan *chan, struct sk_buff *skb);
extern int vss_chan_push_raw(
		struct ks_chan *chan,
		struct ks_streamframe *sf);
extern int vss_chan_get_pressure(struct ks_chan *chan);

#endif

#endif

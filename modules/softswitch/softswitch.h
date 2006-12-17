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

#define kss_MODULE_NAME "softswitch"
#define kss_MODULE_PREFIX kss_MODULE_NAME ": "
#define kss_MODULE_DESCR "Kstreamer softswitch"

enum kss_push_frame_return_codes
{
	KSS_TX_OK,
	KSS_TX_BUSY,
	KSS_TX_FULL,
	KSS_TX_LOCKED,
};

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


struct kss_softswitch
{
	struct ks_node ks_node;

//	u8 buf[1024];

//	unsigned long long overhead_cycles;
//	unsigned long long overhead;
//
};

struct kss_chan_from_ops
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

struct kss_chan_to_ops
{
	void (*wake_queue)(struct ks_chan *chan);
/*

	void (*rx_error)(struct visdn_leg *leg,
		enum visdn_leg_rx_error_code code);
	void (*tx_error)(struct visdn_leg *leg,
		enum visdn_leg_tx_error_code code);
*/
};

extern struct kss_softswitch kss_softswitch;

extern int kss_chan_push_frame(struct ks_chan *chan, struct sk_buff *skb);
extern int kss_chan_push_raw(
		struct ks_chan *chan,
		struct ks_streamframe *sf);
extern int kss_chan_get_pressure(struct ks_chan *chan);

extern void kss_chan_wake_queue(struct ks_chan *chan);

#endif

#endif

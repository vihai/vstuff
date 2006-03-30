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

#ifndef _LAPD_IN_H
#define _LAPD_IN_H

int lapd_backlog_rcv(struct sock *sk, struct sk_buff *skb);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
extern int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *dev,
	struct packet_type *pt);
#else
extern int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *dev,
	struct packet_type *pt,
	struct net_device *orig_dev);
#endif

int lapd_mgmt_backlog_rcv(struct sock *sk, struct sk_buff *skb);

#endif

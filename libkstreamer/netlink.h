/*
 * Userland Kstreamer Helper Routines
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBKSTREAMER_NETLINK_H
#define _LIBKSTREAMER_NETLINK_H

#include <linux/types.h>

#include "conn.h"

struct sk_buff;

struct nlmsghdr *ks_nlmsg_put(
	struct sk_buff *skb, __u32 pid, __u32 seq,
	__u16 message_type, __u16 flags, int payload_size);

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len);

int ks_send_next_packet(struct ks_conn *conn);
void ks_netlink_receive(struct ks_conn *conn);

//int ks_netlink_sendmsg(struct ks_conn *conn, struct sk_buff *skb);

#endif

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

#include "conn.h"

struct nlmsghdr *ks_nlmsg_put(
	struct sk_buff *skb, __u32 pid, __u32 seq,
	__u16 message_type, __u16 flags, int payload_size);

int ks_send_topology_update_req(struct ks_conn *conn);
int ks_send_noop(struct ks_conn *conn);

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len);

void ks_netlink_receive(struct ks_conn *conn);
void ks_netlink_waitloop(struct ks_conn *conn);
int ks_netlink_sendmsg(struct ks_conn *conn, struct sk_buff *skb);

#endif

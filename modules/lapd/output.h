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

#ifndef _LAPD_OUT_H
#define _LAPD_OUT_H

#include "proto.h"

void lapd_out_queue_flush(struct lapd_device *dev);
void lapd_out_queue_drop(struct lapd_device *dev);

struct sk_buff *lapd_alloc_data_request_skb(
	struct lapd_device *dev,
	unsigned int size);

int lapd_prepare_iframe(struct lapd_sock *lapd_sock,
	struct sk_buff *skb);

int lapd_prepare_uframe(struct lapd_sock *lapd_sock, struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f);

void lapd_sock_queue_uframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb);

int lapd_send_uframe(struct lapd_sock *lapd_sock,
	enum lapd_uframe_function function,
	int p_f,
	void *data, int datalen);

int lapd_ph_data_request(struct sk_buff *skb);

void lapd_out_init(void);
void lapd_out_exit(void);

#endif

/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_OUT_H
#define _LAPD_OUT_H

#include "lapd_proto.h"

int lapd_send_iframe(struct lapd_sock *lapd_sock, u8 sapi, u8 tei,
	void *data, int datalen);
int lapd_prepare_iframe(struct lapd_sock *lapd_sock,
	struct sk_buff *skb);

int lapd_prepare_uframe(struct lapd_sock *lapd_sock, struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f);

void lapd_queue_completed_uframe(struct lapd_sock *lapd_sock, struct sk_buff *skb);
int lapd_send_completed_uframe(struct sk_buff *skb);

int lapd_send_uframe(struct lapd_sock *lapd_sock,
	enum lapd_uframe_function function,
	int p_f,
	void *data, int datalen);

int lapd_send_frame(struct sk_buff *skb);

int lapd_send_sframe(struct lapd_sock *lapd_sock,
	enum lapd_cr c_r,
	enum lapd_uframe_function function, int p_f);
//void lapd_retransmit_from(struct lapd_sock *lapd_sock, int n_s);

#endif

#ifndef _LAPD_OUT_H
#define _LAPD_OUT_H

#include "lapd_proto.h"

int lapd_send_iframe(struct sock *sk, u8 sapi, u8 tei,
	void *data, int datalen);
int lapd_prepare_iframe(struct sock *sk,
	struct sk_buff *skb);

int lapd_prepare_uframe(struct sock *sk, struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f);

void lapd_queue_completed_uframe(struct sock *sk, struct sk_buff *skb);
int lapd_send_completed_uframe(struct sk_buff *skb);

int lapd_send_uframe(struct sock *sk,
	enum lapd_uframe_function function,
	int p_f,
	void *data, int datalen);

inline int lapd_send_frame(struct sk_buff *skb);

int lapd_send_sframe(struct sock *sk,
	enum lapd_cr c_r,
	enum lapd_uframe_function function, int p_f);
void lapd_retransmit_from(struct sock *sk, int n_s);
void lapd_flush_uqueue(struct sock *sk);

static inline void lapd_discard_uqueue(struct sock *sk)
{
	skb_queue_purge(&lapd_sk(sk)->u_queue);
}


#endif

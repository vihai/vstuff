
#ifndef _LAPD_MULTI_H
#define _LAPD_MULTI_H

static inline void lapd_enter_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = TRUE;

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Entering timer recovery condition\n",
		lo->dev->name);
}

static inline void lapd_leave_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = FALSE;

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Leaving timer recovery condition\n",
		lo->dev->name);
}

static inline int lapd_is_valid_nr(struct lapd_opt *lo, int n_r)
{
	// V(A) <= N(R) <= V(S)

	return (n_r - lo->v_a) % 128 <=
		(lo->v_s - lo->v_a) % 128;
}

void lapd_start_multiframe_establishment(struct sock *sk);
void lapd_start_multiframe_release(struct sock *sk);
void lapd_multiframe_established(struct sock *sk);
void lapd_multiframe_released(struct sock *sk);
void lapd_send_enquiry_response(struct sock *sk);
void lapd_start_invalid_nr_recovery(struct sock *sk);
void lapd_send_enquiry_response(struct sock *sk);
void lapd_T200_timer(unsigned long data);
int lapd_multiframe_wait_for_establishment(struct sock *sk);
int lapd_multiframe_wait_for_release(struct sock *sk);

void lapd_handle_socket_iframe(struct sock *sk,
	struct sk_buff *skb);
void lapd_handle_socket_sframe(struct sock *sk,
	struct sk_buff *skb);

#endif

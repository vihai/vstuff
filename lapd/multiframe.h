
#ifndef _LAPD_MULTI_H
#define _LAPD_MULTI_H

#ifdef SOCK_DEBUGGING
#define lapd_debug_multiframe(sk, format, arg...)	\
	if ((sk)->sk_debug)				\
		printk(KERN_DEBUG "lapd: "		\
			"%s "				\
			"V(S)=%u V(R)=%u V(A)=%u: "	\
			format,				\
			lapd_sk(sk)->dev->name,		\
			lapd_sk(sk)->v_s,		\
			lapd_sk(sk)->v_r,		\
			lapd_sk(sk)->v_a,		\
			## arg)
#else
#define lapd_debug_multiframe(sk, format, arg...)	\
		do { } while (0)
#endif

#define lapd_printk_multiframe(lvl, sk, format, arg...)	\
	printk(lvl "lapd: "			\
		"%s "				\
		"V(S)=%u V(R)=%u V(A)=%u: "	\
		format,				\
		lapd_sk(sk)->dev->name,		\
		lapd_sk(sk)->v_s,		\
		lapd_sk(sk)->v_r,		\
		lapd_sk(sk)->v_a,		\
		## arg)


static inline void lapd_enter_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = TRUE;

	lapd_debug_multiframe(sk,
		"Entering timer recovery condition\n");
}

static inline void lapd_leave_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = FALSE;

	lapd_debug_multiframe(sk,
		"Leaving timer recovery condition\n");
}

static inline int lapd_is_valid_nr(struct lapd_opt *lo, int n_r)
{
	/* We need to verify the following disequation with modulo128
	 * arithmetic:
	 *
	 * V(A) <= N(R) <= V(S)
	 *
	 * a-b in modulo 'n' becomes (a-b+n) % n in common airthmetics, so,
	 * subtracting V(A) from all members:
	 *
	 * (V(A)-V(A)+128)%128 <= (N(R)-V(A)+128)%128 <= (V(S)-V(A)+128)%128
	 *
	 * 0 <= (N(R)-V(A)+128)%128 <= (V(S)-V(A)+128)%128
	 *      ^^^^^^^^^^^^^^^^^^^    ^^^^^^^^^^^^^^^^^^^
	 *          Always > 0               Always > 0
	 *
	 * (N(R)-V(A)+128)%128 <= (V(S)-V(A)+128)%128
	 */

	return (n_r - lo->v_a + 128) % 128 <= (lo->v_s - lo->v_a + 128) % 128;
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
int lapd_socket_handle_iframe(struct sock *sk, struct sk_buff *skb);
int lapd_socket_handle_sframe(struct sock *sk, struct sk_buff *skb);
int lapd_queue_completed_iframe(struct sock *sk, struct sk_buff *skb);

#define lapd_start_t200(sk)						\
	do {								\
		lapd_debug_multiframe(sk, "%s:%d T200 START\n",		\
			__FILE__, __LINE__);				\
		sk_reset_timer((sk), &lapd_sk((sk))->T200_timer,	\
			jiffies + lapd_sk((sk))->sap->T200);		\
	} while(0)

#define lapd_stop_t200(sk)						\
	do {								\
		lapd_debug_multiframe(sk, "%s:%d T200 STOP\n",		\
			__FILE__, __LINE__);				\
		sk_stop_timer(sk, &lapd_sk(sk)->T200_timer);		\
	} while(0)

#define lapd_start_t203(sk)						\
	do {								\
		lapd_debug_multiframe(sk, "%s:%d T203 START\n",		\
			__FILE__, __LINE__);				\
		sk_reset_timer((sk), &lapd_sk((sk))->T203_timer,	\
			jiffies + lapd_sk((sk))->sap->T203);		\
	} while(0)

#define lapd_stop_t203(sk)						\
	do {								\
		lapd_debug_multiframe(sk, "%s:%d T203 STOP\n",		\
			__FILE__, __LINE__);				\
		sk_stop_timer(sk, &lapd_sk(sk)->T203_timer);		\
	} while(0)

#endif

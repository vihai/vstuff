/*

*/

#include <linux/skbuff.h>

#include "lapd.h"
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "multiframe.h"

void lapd_dump_queue(struct sock *sk)
{
	printk(KERN_DEBUG "lapd: vvvvvvvvvvvvvvvvvvvvvvv\n");

	struct sk_buff *skb;
	for (skb = sk->sk_write_queue.next;
	     (skb != (struct sk_buff *)&sk->sk_write_queue);
	     skb = skb->next) {

		printk(KERN_DEBUG "lapd: ");

		if (sk->sk_send_head)
		printk("HEAD ");

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
		printk("V(S) = %d\n", hdr->i.n_s);
	}

	printk(KERN_DEBUG "lapd: ^^^^^^^^^^^^^^^^^^^^^^^\n");
}

void lapd_receive_ack(struct sock *sk, int n_r)
{
	struct lapd_opt *lo = lapd_sk(sk);

	// Nothing to ack
	if (n_r == lo->v_a)
		return;

	// The sender is acking some frame
	printk(KERN_DEBUG "lapd: "
		"%s: "
		"received ack for frames from %d to %d-1\n",
		lo->dev->name,
		lo->v_a,
		n_r);

//	lapd_dump_queue(sk);

	struct sk_buff *skb;
	sk_stream_for_retrans_queue(skb, sk) {
		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		__skb_unlink(skb, skb->list);
		sk_stream_free_skb(sk, skb);

		printk(KERN_INFO "lapd: acking frame %d\n",
			hdr->i.n_s);
	}

//	lapd_dump_queue(sk);

	lo->v_a = n_r;
}

void lapd_multiframe_established(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Multiple frame mode established\n",
		lo->dev->name);

	// If we're called at the arrival of
	// SABME, T200 is already stopped anyway
	sk_stop_timer(sk, &lo->T200_timer);

	lo->status = LAPD_DLS_LINK_CONNECTION_ESTABLISHED;

	lo->v_s = 0;
	lo->v_r = 0;
	lo->v_a = 0;

	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->peer_waiting_for_ack = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	// Start idle timer
	sk_reset_timer(sk, &lo->T203_timer,
		jiffies + lo->sap->T203);

	// Notify the socket status changed
	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
	}
}

void lapd_multiframe_released(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Multiple frame mode released\n",
		lo->dev->name);

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_LINK_CONNECTION_RELEASED;

	sk_stop_timer(sk, &lo->T200_timer);

	if (sock_flag(sk, SOCK_DEAD)) {
		// We're waiting for disconnection, unhash and let socket die
		sock_hold(sk);
		lapd_unhash(sk);
		sock_put(sk);
	} else {
		// Notify the socket status changed
		sk->sk_state_change(sk);
	}
}

void lapd_start_multiframe_establishment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Starting multiframe establishment\n",
		lo->dev->name);

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->sap->T200);
	lo->N200_cnt = 0;

	// Other checks

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_AWAITING_ESTABLISH;

	lapd_send_uframe(sk, SABME, 0, NULL, 0);
}

void lapd_start_multiframe_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"Starting multiframe release\n",
		lo->dev->name);

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_AWAITING_RELEASE;

	lo->N200_cnt = 0;

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->sap->T200);

	lapd_send_uframe(sk, DISC, 0, NULL, 0);
}

void lapd_send_enquiry_response(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->peer_waiting_for_ack = FALSE;

	if (lo->me_busy) {
		lapd_send_sframe(sk, RESPONSE, RNR, 1);
	} else {
		lapd_send_sframe(sk, RESPONSE, RR, 1);
	}
}

void lapd_transmit_enquiry(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->peer_waiting_for_ack = TRUE;

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->sap->T200);

	if (lo->me_busy) {
		lapd_send_sframe(sk, COMMAND, RNR, 1);
	} else {
		lapd_send_sframe(sk, COMMAND, RR, 1);
	}
}

void lapd_start_invalid_nr_recovery(struct sock *sk)
{
	// Signal error to user TODO

	lapd_start_multiframe_establishment(sk);
}

void lapd_T200_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"T200\n",
		lo->dev->name);

	if (lo->status == LAPD_DLS_AWAITING_ESTABLISH) {
		if (lo->N200_cnt > lo->sap->N200) {
			lapd_multiframe_released(sk);

			sk->sk_err = -ETIMEDOUT;

			goto max_count;
		}

		lo->N200_cnt++;

		lapd_send_uframe(sk, SABME, 0, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_RELEASE) {
		if (lo->N200_cnt > lo->sap->N200) {
			lapd_multiframe_released(sk);

			goto max_count;
		};

		lo->N200_cnt++;

		lapd_send_uframe(sk, DISC, 0, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		if (!lo->in_timer_recovery) {
			lapd_enter_timer_recovery(sk);

			lo->N200_cnt = 0;
		} else {
			lo->N200_cnt++;
		}

		// Here I'd prefer to implement point a) 3 of 5.6.7

		lapd_transmit_enquiry(sk);
	} else {
		printk(KERN_ERR "lapd: "
			"%s: "
			"Unexpected T200 in state %d\n",
			lo->dev->name,
			lo->status);

		goto err_unexpected_t200;
	}

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->sap->T200);

err_unexpected_t200:
max_count:

	sock_put(sk);
}

int lapd_multiframe_wait_for_establishment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		// This should avoid a race condition with very fast responses
		// (fake driver always triggers the race condition)
		if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED)
			break;
		if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED) {
			err = -ECONNREFUSED;
			break;
		}

		release_sock(sk);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED)
			break;

		if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED) {
			err = -ECONNREFUSED;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			printk(KERN_ERR "lapd: Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}

		if (sk->sk_err < 0) {
			err = sk->sk_err;
			break;
		}
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}


int lapd_multiframe_wait_for_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		// This should avoid a race condition with very fast responses
		// (fake driver always triggers the race condition)
		if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED)
			break;

		release_sock(sk);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			printk(KERN_ERR "lapd: Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}

		err = sock_error(sk);
		if (err)
			break;
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}

int lapd_prepare_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if ((lo->v_s - lo->v_a + 128) % 128
	     > lo->sap->k) {
		// We should not trasnmit (see 5.6.1)
	}

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = lo->sapi;
	// I-frames are always commands
	hdr->addr.c_r = lo->nt_mode ? 1 : 0;
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lapd_get_tei(lo);
	hdr->addr.ea2 = 1;

	hdr->i.ft = 0;
	hdr->i.p = 0; // FIXME

	return 0;
}

static void lapd_iframe_queue(struct sock *sk, struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

	hdr->i.n_s = lo->v_s;
	hdr->i.n_r = lo->v_r;

	lo->v_s = (lo->v_s + 1) % 128;

	skb_queue_tail(&sk->sk_write_queue, skb);
	sk_charge_skb(sk, skb);

	/* Queue it, remembering where we must start sending. */
	if (!sk->sk_send_head)
		sk->sk_send_head = skb;

}

void lapd_flush_queue(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

//	lapd_dump_queue(sk);

	struct sk_buff *skb;
	while ((skb = sk->sk_send_head)) {

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
		printk(KERN_DEBUG "lapd: "
			"%s: "
			"transmitting frame %d, send_head=%p\n",
			lo->dev->name,
			hdr->i.n_s, sk->sk_send_head);

		// Update send_head before transmitting or else we may receive
		// the ack immediately, so, being unable to actually ack frames
		// The fake driver always triggered this
		sk->sk_send_head = skb->next;

		if (sk->sk_send_head == (struct sk_buff *)&sk->sk_write_queue)
		        sk->sk_send_head = NULL;

		sk_reset_timer(sk, &lo->T200_timer,
			jiffies + lo->sap->T200);

		dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));
	}
	
//	lapd_dump_queue(sk);
}

int lapd_send_completed_iframe(struct sk_buff *skb)
{
	lapd_iframe_queue(skb->sk, skb);
	lapd_flush_queue(skb->sk);

	return 0;
}

int lapd_prepare_sframe(struct sock *sk,
	struct sk_buff *skb,
	enum lapd_cr c_r,
	enum lapd_sframe_function function, int p_f)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_ON(!lo->dev);

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = lo->sapi;
	hdr->addr.c_r = lapd_make_cr(lo->nt_mode, c_r);
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lapd_get_tei(lo);
	hdr->addr.ea2 = 1;

	hdr->control  = lapd_sframe_make_control(function);
	hdr->control2 = lapd_sframe_make_control2(lo->v_r, p_f);

	return 0;
}

int lapd_send_sframe(struct sock *sk,
	enum lapd_cr c_r,
	enum lapd_uframe_function function, int p_f)
{
	int err = 0;

	struct sk_buff *skb;
		skb = sock_alloc_send_skb(sk,
			sizeof(struct lapd_hdr_e),
			0, &err);
// FIXME		(msg->msg_flags & MSG_DONTWAIT), &err);
	if (!skb) return err;

	err = lapd_prepare_sframe(sk, skb,
			c_r, function, p_f);
	if (err < 0) {
		kfree_skb(skb);
		return err;
	}

	return lapd_send_frame(skb);
}

void lapd_retransmit_from(struct sock *sk, int n_s)
{
	struct sk_buff *skb;
	struct lapd_opt *lo = lapd_sk(sk);

        sk_stream_for_retrans_queue(skb, sk) {
                struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		printk(KERN_DEBUG "lapd: "
			"%s: "
			"Retransmitting frame N(S)=%d\n",
			lo->dev->name,
			hdr->i.n_s);

		dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));

		if (hdr->i.n_s == n_s)
			break;
	}
}

void lapd_handle_socket_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"received i-frame\n",
		lo->dev->name);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"V(S)=%u V(R)=%u V(A)=%u: received N(S)=%d N(R)=%d\n",
		lo->dev->name,
		lo->v_s,
		lo->v_r,
		lo->v_a,
		hdr->i.n_s,
		hdr->i.n_r);

	if (lo->status != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		printk(KERN_WARNING "lapd: "
			"%s: "
			"received i-frame while link not established"
			" (status=%d)\n",
			lo->dev->name,
			lo->status);
		return;
	}

	if (lo->me_busy) {
		if (hdr->i.p) lapd_send_enquiry_response(sk);

		// Discard frame
		return;
	}

	lapd_receive_ack(sk, hdr->i.n_r);

	if (hdr->i.n_s == lo->v_r) {
		// N(S) == V(R), the frame is the one we're expecting

		// Do we ever enter busy condition?
		// If yes, we should set me_busy here

		// Ok, we're going to expect the next one (oooh!)
		lo->v_r = (lo->v_r + 1) % 128;

		lo->rejection_exception = FALSE;

		// The sender is polling us
		if (hdr->i.p)
			lapd_send_enquiry_response(sk);
		else
			lo->peer_waiting_for_ack = TRUE;

		skb_pull(skb, sizeof(struct lapd_hdr_e));

		// pass it to the user.
		int err = sock_queue_rcv_skb(sk, skb);
		if (err == -ENOMEM) {
		} else {
			printk(KERN_ERR "lapd: sock_queue_rcv_skb err: %d\n",
				err);

			send_rej();
		}
	} else {
		// uhuh... the frame is not in sequence, we have to recover

		if (lo->rejection_exception) {
			if (hdr->i.p)
				lapd_send_enquiry_response(sk);
		} else {
			lapd_send_sframe(sk, RESPONSE, REJ, hdr->i.p);
			lo->rejection_exception = TRUE;
			lo->peer_waiting_for_ack = FALSE;
		}
	}

	if (!lapd_is_valid_nr(lo, hdr->i.n_r)) {
		printk(KERN_WARNING "lapd: "
			"%s: "
			"invalid N(R)=%d (V(s)=%d, V(a)=%d)\n",
			lo->dev->name,
			hdr->i.n_r,
			lo->v_s,
			lo->v_a);

		lapd_start_invalid_nr_recovery(sk);

		return;
	}

	if (!lo->peer_busy) {
		if (hdr->i.n_r == lo->v_s) {
			sk_stop_timer(sk, &lo->T200_timer);

			sk_reset_timer(sk, &lo->T203_timer,
				jiffies + lo->sap->T203);

		} else if (hdr->i.n_r != lo->v_a) {
			sk_reset_timer(sk, &lo->T200_timer,
				jiffies + lo->sap->T200);
		}
	}

	if (lo->peer_waiting_for_ack) {
		lo->peer_waiting_for_ack = FALSE;
		lapd_send_sframe(sk, RESPONSE, RR, 0);
	}
		
}

static inline void lapd_handle_socket_sframe_rr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	printk(KERN_DEBUG "lapd: "
		"%s: "
		"received s-frame RR\n",
		lo->dev->name);

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

	// Always clear peer busy condition
	lo->peer_busy = FALSE;

	if (hdr->s.n_r == lo->v_s) {
		printk(KERN_DEBUG
			"lapd: "
			"%s: "
			"All frames acked, stopping T200\n",
			lo->dev->name);

		sk_stop_timer(sk, &lo->T200_timer);

		sk_reset_timer(sk, &lo->T203_timer,
			jiffies + lo->sap->T203);
	} else if (hdr->s.n_r != lo->v_a) {
		sk_reset_timer(sk, &lo->T200_timer,
			jiffies + lo->sap->T200);
	}
}

static inline void lapd_handle_socket_sframe_rnr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"received s-frame REJ\n",
		lo->dev->name);

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

	// Always set peer busy condition
	lo->peer_busy = TRUE;

	if (lo->v_a != hdr->s.n_r) {
	}

	sk_stop_timer(sk, &lo->T203_timer);

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->sap->T200);
}

static inline void lapd_handle_socket_sframe_rej(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"received s-frame REJ\n",
		lo->dev->name);

	// Always clear peer busy condition
	lo->peer_busy = FALSE;

	lapd_retransmit_from(sk, hdr->s.n_r);

	lapd_flush_queue(sk);

	sk_stop_timer(sk, &lo->T200_timer);

	sk_reset_timer(sk, &lo->T203_timer,
		jiffies + lo->sap->T203);
}

void lapd_handle_socket_sframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->status != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		printk(KERN_WARNING "lapd: "
			"%s: "
			"received s-frame while link not established"
			" (status=%d)\n",
			lo->dev->name,
			lo->status);
		return;
	}

	printk(KERN_DEBUG "lapd: "
		"%s: "
		"s-Frame: V(S)=%u V(R)=%u V(A)=%u: received N(R)=%d\n",
		lo->dev->name,
		lo->v_s,
		lo->v_r,
		lo->v_a,
		hdr->s.n_r);

	lapd_receive_ack(sk, hdr->s.n_r);

	if (lapd_rx_is_command(lo->nt_mode, hdr->addr.c_r) && hdr->s.p_f)
		lapd_send_enquiry_response(sk);

	if (!lapd_is_valid_nr(lo, hdr->s.n_r)) {
		printk(KERN_WARNING "lapd: "
			"%s: "
			"invalid N(R)=%d (V(s)=%d, V(a)=%d)\n",
			lo->dev->name,
			hdr->s.n_r,
			lo->v_s,
			lo->v_a);

		lapd_start_invalid_nr_recovery(sk);

		return;
	}

	if (lo->in_timer_recovery) {
		if (!lapd_rx_is_command(lo->nt_mode,hdr->addr.c_r) && hdr->s.p_f) {
			lapd_retransmit_from(sk, hdr->s.n_r);
			lapd_leave_timer_recovery(sk);
		}
	}

	switch (lapd_sframe_function(hdr->control)) {
	case RR: lapd_handle_socket_sframe_rr(sk, skb); break;
	case RNR: lapd_handle_socket_sframe_rnr(sk, skb); break;
	case REJ: lapd_handle_socket_sframe_rej(sk, skb); break;
	}
}


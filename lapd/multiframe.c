/*

*/

#include <linux/skbuff.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "multiframe.h"

void lapd_dump_queue(struct sock *sk)
{
	lapd_debug_multiframe(sk, "vvvvvvvvvvvvvvvvvvvvvvv\n");

	struct sk_buff *skb;
	for (skb = sk->sk_write_queue.next;
	     (skb != (struct sk_buff *)&sk->sk_write_queue);
	     skb = skb->next) {

		lapd_debug_multiframe(sk, "");

		if (sk->sk_send_head)
		printk("HEAD ");

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
		printk("V(S) = %d\n", hdr->i.n_s);
	}

	lapd_debug_multiframe(sk, "^^^^^^^^^^^^^^^^^^^^^^^\n");
}

void lapd_ack_frames(struct sock *sk, int n_r)
{
	struct lapd_opt *lo = lapd_sk(sk);

	// Nothing to ack
	if (n_r == lo->v_a)
		return;

	// The sender is acking some frame
	lapd_debug_multiframe(sk,
		"received ack for frames from %d to %d-1\n",
		lo->v_a,
		n_r);

//	lapd_dump_queue(sk);

	struct sk_buff *skb;
	for (skb = (sk)->sk_write_queue.next;
	    (skb != (struct sk_buff *)&(sk)->sk_write_queue);) {
		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		if (hdr->i.n_s == n_r) break;

		lapd_debug_multiframe(sk,
			"peer acked frame %d\n",
			hdr->i.n_s);

		struct sk_buff *old_skb = skb;

		skb = skb->next;

		__skb_unlink(old_skb, old_skb->list);
		__kfree_skb(old_skb);
	}

//	lapd_dump_queue(sk);
	if (n_r == lo->v_s) {
		lapd_debug_multiframe(sk,
			"All frames acked, stopping T200\n");

		lapd_stop_t200(sk);
		lapd_start_t203(sk);
	} else {
		lapd_debug_multiframe(sk,
			"Not all outstanding frames acked, restarting T200\n");

		lapd_start_t200(sk);
	}


	lo->v_a = n_r;
}

void lapd_multiframe_established(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk,
		"Multiple frame mode established\n");

	// If we're called at the arrival of
	// SABME, T200 is already stopped anyway
	lapd_stop_t200(sk);

	lo->status = LAPD_DLS_LINK_CONNECTION_ESTABLISHED;

	lo->v_s = 0;
	lo->v_r = 0;
	lo->v_a = 0;

	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	// Start idle timer
	lapd_start_t203(sk);

	// Notify the socket status changed
	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
	}
}

void lapd_multiframe_released(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk,
		"Multiple frame mode released\n");

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_LINK_CONNECTION_RELEASED;

	lapd_stop_t200(sk);

	if (sk->sk_state == TCP_CLOSING) {
		// Defers unhash
		sk_reset_timer(sk, &sk->sk_timer, jiffies + HZ);
	}

	if (!sock_flag(sk, SOCK_DEAD)) {
		// Notify the socket status changed
		sk->sk_state_change(sk);
	}
}

void lapd_start_multiframe_establishment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk,
		"Starting multiframe establishment\n");

	lapd_start_t200(sk);
	lo->N200_cnt = 0;

	// Other checks

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_AWAITING_ESTABLISH;

	lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
}

void lapd_start_multiframe_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk,
		"Starting multiframe release\n");

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LAPD_DLS_AWAITING_RELEASE;

	lo->N200_cnt = 0;

	lapd_start_t200(sk);

	lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
}

void lapd_T200_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	bh_lock_sock(sk);

	lapd_debug_multiframe(sk, "T200\n");

	if (lo->status == LAPD_DLS_AWAITING_ESTABLISH) {
		if (lo->N200_cnt > lo->sap->N200) {
			lapd_multiframe_released(sk);

			sk->sk_err = -ETIMEDOUT;

			goto max_count;
		}

		lo->N200_cnt++;

		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_RELEASE) {
		if (lo->N200_cnt > lo->sap->N200) {
			lapd_multiframe_released(sk);

			goto max_count;
		};

		lo->N200_cnt++;

		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		if (!lo->in_timer_recovery) {
			lapd_enter_timer_recovery(sk);

			lo->N200_cnt = 0;
		} else {
			if (lo->N200_cnt == lo->sap->N200) {
				// MDL-ERR-IND(I)
				lapd_start_multiframe_establishment(sk);
				goto max_count;
			}

			lo->N200_cnt++;
		}

		lapd_start_t200(sk);

		if (lo->peer_busy) {
			if (lo->me_busy) {
				lapd_send_sframe(sk,
					LAPD_COMMAND,
					LAPD_SFRAME_FUNC_RNR, 1);
			} else {
				lapd_send_sframe(sk,
					LAPD_COMMAND,
					LAPD_SFRAME_FUNC_RR, 1);
			}
		} else {
			// TODO: Retransmit last I-frame with N(S)=V(S)-1
			if (lo->me_busy) {
				lapd_send_sframe(sk,
					LAPD_COMMAND,
					LAPD_SFRAME_FUNC_RNR, 1);
			} else {
				lapd_send_sframe(sk,
					LAPD_COMMAND,
					LAPD_SFRAME_FUNC_RR, 1);
			}
		}
	} else {
		lapd_printk_multiframe(KERN_ERR, sk,
			"Unexpected T200 in state %d\n",
			lo->status);

		goto err_unexpected_t200;
	}

	lapd_start_t200(sk);

err_unexpected_t200:
max_count:

	bh_unlock_sock(sk);

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
			lapd_printk_sk(KERN_ERR, sk,
				"Connect timed out?!?\n");

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
			lapd_printk_sk(KERN_ERR, sk,
				"Connect timed out?!?\n");

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

static void lapd_set_retransmission(struct sock *sk)
{
	if (!skb_queue_empty(&sk->sk_write_queue)) {
		sk->sk_send_head = sk->sk_write_queue.next;
	}
}

static void lapd_run_output_queue(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	int n_out_frames = 0;

	if (lo->in_timer_recovery)
		return;

//	lapd_dump_queue(sk);

	struct sk_buff *skb;
	for (skb = sk->sk_send_head;
	     skb &&
	       skb != (struct sk_buff *)&sk->sk_write_queue &&
	       n_out_frames < lo->sap->k;
	     skb = skb->next, sk->sk_send_head = skb, n_out_frames++) {

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		hdr->i.n_s = lo->v_s;
		hdr->i.n_r = lo->v_r;

/* Domani, creare una sola funzione che faccia il flush della coda

lapd_queue aggiunge in fondo e chiama lapd_flush se può
lapd_flush parte da send_head e butta fuori max k frames
lapd_retransmit imposta send_head e chiama lapd_flush
*/

		lapd_debug_multiframe(sk,
			"Transmitting i-frame N(S)=%d\n",
			hdr->i.n_s);

		lapd_start_t200(sk);

		dev_queue_xmit(skb_copy(skb, GFP_ATOMIC));

		lo->v_s = (lo->v_s + 1) % 128;
	}
	
	if (sk->sk_send_head ==
	    (struct sk_buff *)&sk->sk_write_queue)
		sk->sk_send_head = NULL;

//	lapd_dump_queue(sk);
}

int lapd_queue_completed_iframe(struct sock *sk, struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk,
		"Queueing i-frame\n");

	skb->dev = lo->dev;

	skb_queue_tail(&sk->sk_write_queue, skb);

	if (!sk->sk_send_head)
		sk->sk_send_head = skb;

	lapd_run_output_queue(sk);

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
	skb = alloc_skb(sizeof(struct lapd_hdr_e), GFP_ATOMIC);
	if (!skb) {
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	err = lapd_prepare_sframe(sk, skb,
			c_r, function, p_f);
	if (err < 0) {
		goto err_prepare_sframe;
	}

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);
	lapd_debug_multiframe(sk,
		"Transmitting s-frame %s N(R)=%d\n",
		lapd_sframe_function_name(lapd_sframe_function(hdr->control)),
		lo->v_r);

	return lapd_send_frame(skb);

err_prepare_sframe:
	kfree_skb(skb);
err_alloc_skb:
	return err;
}

int lapd_socket_handle_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	if (atomic_read(&sk->sk_rmem_alloc) <
		(unsigned)sk->sk_rcvbuf &&
	    lo->me_busy) {

		lapd_debug_multiframe(sk,
			"Input queue not full anymore,"
			" exiting busy condition\n");

		lo->me_busy = FALSE;
	}

	lapd_debug_multiframe(sk,
		"Received i-frame N(S)=%d N(R)=%d\n",
		hdr->i.n_s,
		hdr->i.n_r);

	if (lo->status != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_printk_sk(KERN_WARNING, sk,
			"received i-frame while link not established"
			" (status=%d)\n",
			lo->status);
		return FALSE;
	}

	if (skb->len > lo->dev->mtu) {
		lapd_printk_sk(KERN_WARNING, sk,
			"received i-frame longer than mtu (%d > %d),"
			" rejecting\n",
			skb->len,
			lo->dev->mtu);

		lapd_frame_reject(sk, skb, 0, 0, 1, 0);

		return FALSE;
	}

	if (!lapd_is_valid_nr(lo, hdr->i.n_r)) {
		lapd_printk_multiframe(KERN_WARNING, sk,
			"invalid N(R)=%d\n",
			hdr->i.n_r);

		lapd_frame_reject(sk, skb, 0, 0, 0, 1);

		return FALSE;
	}

	lapd_ack_frames(sk, hdr->i.n_r);

	if (lo->me_busy) {
		if (hdr->i.p) {
			lapd_send_sframe(sk, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_RNR, 1);
		}

		// Discard frame
		return FALSE;
	}

	if (hdr->i.n_s != lo->v_r) {
		// uhuh... the frame is not in sequence, we have to recover

		lapd_debug_multiframe(sk,
			"Not in-sequence frame received, dropping\n");

		if (lo->rejection_exception) {
			if (hdr->i.p) {
				if (lo->me_busy) {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RNR, 1);
				} else {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 1);
				}
			}
		} else {
			// Not in REJ exception, enter rej exception

			lapd_debug_multiframe(sk,
				"Not in-sequence frame received,"
				" entering rejection exception\n");

			lapd_send_sframe(sk, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_REJ, hdr->i.p);

			lo->rejection_exception = TRUE;
		}

		return FALSE;
	}

	// N(S) == V(R), the frame is the one we're expecting

	if (lo->rejection_exception) {
		lapd_debug_multiframe(sk,
			"In sequence frame received,"
			" exiting rejection exception\n");

		lo->rejection_exception = FALSE;
	}

	// The receive queue is full 
	if (atomic_read(&sk->sk_rmem_alloc) >= (unsigned)sk->sk_rcvbuf) {

		lapd_debug_multiframe(sk,
			"Incoming queue full, entering busy condition\n");

		lo->me_busy = TRUE;

		lapd_send_sframe(sk, LAPD_RESPONSE,
			LAPD_SFRAME_FUNC_RNR, hdr->i.p);
	} else {
		// pass it to the user.

		skb_pull(skb, sizeof(struct lapd_hdr_e));

		// Remember those values, after sock_queue_rcv_skb, skb may
		// not be valid anymore
		int p = hdr->i.p;
		int n_s = hdr->i.n_s;

		skb->dev = NULL;
		skb_set_owner_r(skb, sk);

		int skb_len = skb->len;
		skb_queue_tail(&sk->sk_receive_queue, skb);

		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_data_ready(sk, skb_len);

		lapd_debug_multiframe(sk,
			"Acking frame N(S)=%d\n", n_s);

		// Ok, we're going to expect the next one (oooh!)
		lo->v_r = (lo->v_r + 1) % 128;

		// If we have an outgoing I frame and P=0 we may send it
		// instead
		lapd_send_sframe(sk, LAPD_RESPONSE,
			LAPD_SFRAME_FUNC_RR, p);

		return TRUE;
	}

	return FALSE;
}

static inline int lapd_socket_handle_sframe_rr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

	if (lo->peer_busy) {
		lapd_debug_multiframe(sk,
			"Leaving peer busy condition\n");

		lo->peer_busy = FALSE;
	}

	if (lo->in_timer_recovery) {
		if (!lapd_rx_is_command(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {

			lapd_stop_t200(sk);
			lapd_start_t203(sk);

			lo->v_s = hdr->s.n_r;

			lapd_leave_timer_recovery(sk);

			lapd_set_retransmission(sk);
			lapd_run_output_queue(sk);
		}
	} else {
		if (!lapd_rx_is_command(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {
			// MDL-ERR-IND(A)
		}
	}

	return FALSE;
}

static inline int lapd_socket_handle_sframe_rnr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

	if (!lo->peer_busy) {
		lapd_debug_multiframe(sk,
			"Entering peer busy condition\n");

		lo->peer_busy = TRUE;
	}

	if (lo->in_timer_recovery) {
		if (lapd_rx_is_response(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {
			lapd_start_t200(sk);

			lo->v_s = hdr->s.n_r;

			lapd_leave_timer_recovery(sk);
		}
	} else {
		if (lapd_rx_is_command(lo->nt_mode, hdr->addr.c_r)) {
			if (hdr->s.n_r == lo->v_s) {
				lapd_start_t200(sk);

				if (!lo->peer_busy)
					lapd_stop_t203(sk);
			} else {
				lapd_start_t200(sk);
			}
		} else {
			if (hdr->s.p_f) {
				// MDL-ERR-IND(A)
			}
		}
	}

	return FALSE;
}

static inline int lapd_socket_handle_sframe_rej(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->peer_busy) {
		lapd_debug_multiframe(sk,
			"Leaving peer busy condition\n");

		lo->peer_busy = FALSE;
	}

	if (lo->in_timer_recovery) {
		if (lapd_rx_is_response(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {
			lo->v_s = hdr->s.n_r;

			lapd_stop_t200(sk);
			lapd_start_t203(sk);

			lapd_leave_timer_recovery(sk);

			lapd_set_retransmission(sk);
			lapd_run_output_queue(sk);
		}
	} else {
		lapd_stop_t200(sk);
		lapd_start_t203(sk);

		lo->v_s = hdr->s.n_r;

		lapd_set_retransmission(sk);
		lapd_run_output_queue(sk);
	}

	return FALSE;
}

int lapd_socket_handle_sframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	if (atomic_read(&sk->sk_rmem_alloc) <
		(unsigned)sk->sk_rcvbuf &&
	    lo->me_busy) {

		lapd_debug_multiframe(sk,
			"Input queue not full anymore,"
			" exiting busy condition\n");

		lo->me_busy = FALSE;
	}

	lapd_debug_multiframe(sk,
		"Received s-frame %s N(R)=%d\n",
		lapd_sframe_function_name(lapd_sframe_function(hdr->control)),
		hdr->s.n_r);

	if (lo->status != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_printk_sk(KERN_WARNING, sk,
			"received s-frame while link not established"
			" (status=%d)\n",
			lo->status);
		return FALSE;
	}

	if (skb->len != 4) {
		lapd_debug_multiframe(sk,
			"received s-frame with wrong size (%d), rejecting\n",
			skb->len);

		lapd_frame_reject(sk, skb, 1, 1, 0, 0);

		return FALSE;
	}

	if (lapd_rx_is_command(lo->nt_mode, hdr->addr.c_r) &&
	    hdr->s.p_f) {
		if (lo->me_busy) {
			lapd_send_sframe(sk, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_RNR, 1);
		} else {
			lapd_send_sframe(sk, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_RR, 1);
		}
	}

	if (!lapd_is_valid_nr(lo, hdr->s.n_r)) {
		lapd_printk_multiframe(KERN_WARNING, sk,
			"invalid N(R)=%d\n",
			hdr->s.n_r);

		// MDL-ERR-IND(J)
		// if F=1 MDL-ERR-IND(A)
		lapd_frame_reject(sk, skb, 0, 0, 0, 1);

		return FALSE;
	}

	lapd_ack_frames(sk, hdr->s.n_r);

	int queued = FALSE;
	switch (lapd_sframe_function(hdr->control)) {
	case LAPD_SFRAME_FUNC_RR:
		queued = lapd_socket_handle_sframe_rr(sk, skb);
	break;

	case LAPD_SFRAME_FUNC_RNR:
		queued = lapd_socket_handle_sframe_rnr(sk, skb);
	break;

	case LAPD_SFRAME_FUNC_REJ:
		queued = lapd_socket_handle_sframe_rej(sk, skb);
	break;

	case LAPD_SFRAME_FUNC_INVALID:
		lapd_frame_reject(sk, skb, 1, 0, 0, 0);
		queued = FALSE;
	break;
	}

	return queued;
}


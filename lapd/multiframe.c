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

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;
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
	for (skb = sk->sk_write_queue.next;
	    (skb != (struct sk_buff *)&sk->sk_write_queue) &&
	     skb != sk->sk_send_head;) {
		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;

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

void lapd_dl_establish_indication(struct sock *sk)
{
	lapd_debug_multiframe(sk,
		"DL-ESTABLISH-INDICATION: Multiple frame mode established\n");

	sk->sk_err = EISCONN;
	sk->sk_error_report(sk);
}

void lapd_dl_release_indication(struct sock *sk)
{
	lapd_debug_sk(sk,
		"DL-RELEASE-INDICATION: Multiple frame mode released\n");

	sk->sk_err = ECONNRESET;
	sk->sk_error_report(sk);

	if (sk->sk_state == TCP_CLOSING) {
		lapd_debug_multiframe(sk, "Scheduling unhash\n");
		// Defers unhash
		sk_reset_timer(sk, &sk->sk_timer, jiffies + 10 * HZ);
	}
}

void lapd_dl_release_confirm(struct sock *sk)
{
	lapd_debug_multiframe(sk,
		"DL-RELEASE-CONFIRM: Multiple frame mode released\n");

	if (sk->sk_state == TCP_CLOSING) {
		lapd_debug_multiframe(sk, "Scheduling unhash\n");
		// Defers unhash
		sk_reset_timer(sk, &sk->sk_timer, jiffies + 10 * HZ);
	}
}

void lapd_T200_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	bh_lock_sock(sk);

        if (sock_owned_by_user(sk)) {
                // Try again later.
			sk_reset_timer(sk, &lo->T200_timer,
                      	  jiffies + HZ/20);

		goto socket_owned;
        }

	lapd_debug_multiframe(sk, "T200 %d\n", sk->sk_state);

	switch (lo->state) {
	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_REESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:

		if (lo->retrans_cnt < lo->sap->N200) {
			lo->retrans_cnt++;

			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);

			lapd_start_t200(sk);
		} else {
			if (lo->state == LAPD_DLS_AWAITING_ESTABLISH) {
				lapd_dl_release_indication(sk);
				// MDL-ERROR-INDICATION(G)

				sk->sk_err = ETIMEDOUT;
			} else if (lo->state == LAPD_DLS_AWAITING_REESTABLISH) {
				lapd_dl_release_indication(sk);
				lapd_discard_iqueue(sk);
			} else if (lo->state == LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE) {
				lapd_discard_iqueue(sk);
				lapd_dl_release_confirm(sk);
			}

			lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		}
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		if (lo->retrans_cnt < lo->sap->N200) {
			lo->retrans_cnt++;

			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);

			lapd_start_t200(sk);
		} else {
			lapd_dl_release_confirm(sk);
			// MDL-ERROR-INDICATION(H)
		}
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		if (!lo->in_timer_recovery) {
			lapd_enter_timer_recovery(sk);

			lo->retrans_cnt = 0;
		} else {
			if (lo->retrans_cnt == lo->sap->N200) {
				// MDL-ERRRO-INDICATION(I)
				lo->retrans_cnt = 0;
				lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME,
					1, NULL, 0);

				lapd_start_t200(sk);
				lapd_change_state(sk, LAPD_DLS_AWAITING_REESTABLISH);

				break;
			}

			lo->retrans_cnt++;
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
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
	case LAPD_DLS_TEI_ASSIGNED:
		lapd_printk(KERN_ERR,
			"Unexpected T200 expire in state %s\n",
			lapd_state_to_text(lo->state));
	break;

	}

socket_owned:
	bh_unlock_sock(sk);

	sock_put(sk);
}

void lapd_T203_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_multiframe(sk, "T203\n");

	bh_lock_sock(sk);

        if (sock_owned_by_user(sk)) {
                // Try again later.
			sk_reset_timer(sk, &lo->T200_timer,
                      	  jiffies + HZ/20);

		goto socket_owned;
        }

	if (lo->in_timer_recovery || lo->peer_busy ||
	    lo->state != LAPD_DLS_LINK_CONNECTION_ESTABLISHED)
		lapd_printk(KERN_ERR,
			"Unexpected T203 expire in state %s\n",
			lapd_state_to_text(lo->state));
		
	lo->retrans_cnt = 0;

	if (lo->me_busy) {
		lapd_send_sframe(sk,
			LAPD_COMMAND,
			LAPD_SFRAME_FUNC_RNR, 1);
	} else {
		lapd_send_sframe(sk,
			LAPD_COMMAND,
			LAPD_SFRAME_FUNC_RR, 1);
	}

	lapd_start_t200(sk);
	lo->in_timer_recovery = TRUE;

socket_owned:

	bh_unlock_sock(sk);

	sock_put(sk);
}

int lapd_prepare_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if ((lo->v_s - lo->v_a + 128) % 128
	     > lo->sap->k) {
		// We should not trasnmit (see 5.6.1)
	}

	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = lo->sapi;
	// I-frames are always commands
	hdr->addr.c_r = lo->nt_mode ? 1 : 0;
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lo->tei;
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

static void lapd_run_iqueue(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->peer_busy || lo->in_timer_recovery)
		return;

//	lapd_dump_queue(sk);

	struct sk_buff *skb;
	for (skb = sk->sk_send_head;
	     skb &&
	       skb != (struct sk_buff *)&sk->sk_write_queue &&
	       lo->v_s != (lo->v_a + lo->sap->k) % 128;
	     skb = skb->next, sk->sk_send_head = skb) {

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;

		hdr->i.n_s = lo->v_s;
		hdr->i.n_r = lo->v_r;

		lapd_debug_multiframe(sk,
			"Transmitting i-frame N(S)=%d\n",
			hdr->i.n_s);

		if (!timer_pending(&lo->T200_timer))
			lapd_start_t200(sk);

		lapd_stop_t203(sk);

		// We need to copy the datagram because we will
		// change N(S) and N(R) in the future
		dev_queue_xmit(skb_copy(skb, GFP_ATOMIC));

		lo->v_s = (lo->v_s + 1) % 128;
	}

	if (lo->v_s == (lo->v_a + lo->sap->k) % 128)
		lapd_debug_multiframe(sk,
			"k reached, not sending more frames\n");
	
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

	lapd_run_iqueue(sk);

	return 0;
}

int lapd_prepare_sframe(struct sock *sk,
	struct sk_buff *skb,
	enum lapd_cr c_r,
	enum lapd_sframe_function function, int p_f)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_ON(!lo->dev);

	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = lo->sapi;
	hdr->addr.c_r = lapd_make_cr(lo->nt_mode, c_r);
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lo->tei;
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

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;
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

static void lapd_dl_data_indication(
	struct sock *sk,
	struct sk_buff *skb)
{
	skb->dev = NULL;
	skb_set_owner_r(skb, sk);

	int skb_len = skb->len;
	skb_queue_tail(&sk->sk_receive_queue, skb);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, skb_len);
}

int lapd_socket_handle_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;
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

	if (lo->state != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_printk_sk(KERN_INFO, sk,
			"received i-frame while link not established"
			" (state=%d)\n",
			lo->state);
		return FALSE;
	}

	if (skb->len > lo->dev->mtu) {
		lapd_printk_sk(KERN_WARNING, sk,
			"received i-frame longer than mtu (%d > %d),"
			" rejecting\n",
			skb->len,
			lo->dev->mtu);

		lapd_frame_reject(sk, skb, LAPD_FE_N201);

		return FALSE;
	}

	// If N(R) is not valid we have to recover
	if (!lapd_is_valid_nr(lo, hdr->i.n_r)) {
		int queued = FALSE;

		lapd_printk_multiframe(KERN_WARNING, sk,
			"invalid N(R)=%d\n",
			hdr->i.n_r);

		// MDL-ERROR-INDICATION(J)

		if (hdr->i.n_s == lo->v_r) {
			if (!lo->me_busy) {
				lo->v_r = (lo->v_r + 1) % 128;

				lapd_dl_data_indication(sk, skb);

				// WARNING: skb is not valid after this
				queued = TRUE;
			}

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
			if (hdr->i.p) {
				if (lo->me_busy) {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RNR, 1);
				} else if (lo->rejection_exception) {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 1);
				} else {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_REJ, 1);
				}
			} else {
				if (!lo->me_busy && !lo->rejection_exception) {
					lapd_send_sframe(sk, LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_REJ, 1);
				}
			}
		}

		lapd_start_t200(sk);

		if (!lo->peer_busy && !lo->in_timer_recovery)
			lapd_stop_t203(sk);

		lo->rejection_exception = FALSE;
		lo->me_busy = FALSE;
		lo->peer_busy = FALSE;

		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
		lapd_change_state(sk, LAPD_DLS_AWAITING_REESTABLISH);

		return queued;
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

		if (!lo->peer_busy) {
			lapd_stop_t200(sk);
			lapd_start_t203(sk);
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
			LAPD_SFRAME_FUNC_RNR, 0);

		return FALSE;
	}

	// Ohhhh finally, we can receive this damned frame :)
	lapd_stop_t200(sk);

	if (hdr->i.n_r == lo->v_s)
		lapd_start_t203(sk);

	// Ok, we're going to expect the next one (oooh!)
	lo->v_r = (lo->v_r + 1) % 128;

	// pass it to the user.
	lapd_dl_data_indication(sk, skb);

	if (hdr->i.p) {
		lapd_send_sframe(sk, LAPD_RESPONSE,
			LAPD_SFRAME_FUNC_RR, 1);
	} else {
		// If we have an outgoing I frame we may send it
		// instead
		lapd_send_sframe(sk, LAPD_RESPONSE,
			LAPD_SFRAME_FUNC_RR, 0);
	}

	return TRUE;
}

static inline int lapd_socket_handle_sframe_rr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;

	if (lo->peer_busy) {
		lapd_debug_multiframe(sk,
			"Leaving peer busy condition\n");

		lo->peer_busy = FALSE;
	}

	if (lo->in_timer_recovery) {
		if (lapd_rx_is_response(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {

			lapd_stop_t200(sk);
			lapd_start_t203(sk);

			lo->v_s = hdr->s.n_r;

			lapd_leave_timer_recovery(sk);

			lapd_set_retransmission(sk);
			lapd_run_iqueue(sk);
		}
	} else {
		if (lapd_rx_is_response(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {
			// MDL-ERROR-INDICATOR(A)
		}
	}

	return FALSE;
}

static inline int lapd_socket_handle_sframe_rnr(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;

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
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;
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
			lapd_run_iqueue(sk);
		}
	} else {
		lapd_stop_t200(sk);
		lapd_start_t203(sk);

		lo->v_s = hdr->s.n_r;

		lapd_set_retransmission(sk);
		lapd_run_iqueue(sk);
	}

	return FALSE;
}

int lapd_socket_handle_sframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->mac.raw;
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

	if (lo->state != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_printk_sk(KERN_WARNING, sk,
			"received s-frame while link not established"
			" (state=%d)\n",
			lo->state);
		return FALSE;
	}

	if (skb->len != 4) {
		lapd_debug_multiframe(sk,
			"received s-frame with wrong size (%d), rejecting\n",
			skb->len);

		lapd_frame_reject(sk, skb, LAPD_FE_LENGTH);

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

	// If N(R) is not valid we have to recover
	if (!lapd_is_valid_nr(lo, hdr->s.n_r)) {
		lapd_printk_multiframe(KERN_WARNING, sk,
			"invalid N(R)=%d\n",
			hdr->s.n_r);

		// MDL-ERROR-INDICATION(J)

		if (lapd_rx_is_response(lo->nt_mode, hdr->addr.c_r) &&
		    hdr->s.p_f) {
			// MDL-ERROR-INDICATION(A)
		}

		lapd_start_t200(sk);

		if (!lo->peer_busy && !lo->in_timer_recovery)
			lapd_stop_t203(sk);

		lo->rejection_exception = FALSE;
		lo->me_busy = FALSE;
		lo->peer_busy = FALSE;

		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
		lapd_change_state(sk, LAPD_DLS_AWAITING_REESTABLISH);

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
//		lapd_frame_reject(sk, skb, 1, 0, 0, 0);
		queued = FALSE;
	break;
	}

	return queued;
}

int lapd_socket_handle_uframe_ua(
	struct sock *sk,
	struct sk_buff *skb)
{
	lapd_debug_sk(sk, "received u-frame UA\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

	if (!hdr->u.p_f) {
		// MDL-ERROR-INDICATION(D)
	}

	switch (lo->state) {
	case LAPD_DLS_TEI_ASSIGNED:
		// MDL-ERROR-INDICATION(C)
	break;

	case LAPD_DLS_AWAITING_ESTABLISH:
		lo->v_s = 0;
		lo->v_r = 0;
		lo->v_a = 0;

		// lapd_dl_establish_confirm(sk);
		lo->in_timer_recovery = FALSE;
		lo->rejection_exception = FALSE;
		lo->me_busy = FALSE;
		lo->peer_busy = FALSE;

		lapd_stop_t200(sk);
		lapd_start_t203(sk);
		lapd_change_state(sk, LAPD_DLS_LINK_CONNECTION_ESTABLISHED);
	break;

	case LAPD_DLS_AWAITING_REESTABLISH:
		lo->v_s = 0;
		lo->v_r = 0;
		lo->v_a = 0;

		lo->in_timer_recovery = FALSE;
		lo->rejection_exception = FALSE;
		lo->me_busy = FALSE;
		lo->peer_busy = FALSE;

		if (lo->v_s != lo->v_a) {
			lapd_discard_iqueue(sk);
			lapd_dl_establish_indication(sk);
		}

		lapd_stop_t200(sk);
		lapd_start_t203(sk);
		lapd_change_state(sk, LAPD_DLS_LINK_CONNECTION_ESTABLISHED);

		lapd_set_retransmission(sk);
		lapd_run_iqueue(sk);
	break;

	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		lapd_discard_iqueue(sk);
		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
		lapd_start_t200(sk);
		lapd_change_state(sk, LAPD_DLS_AWAITING_RELEASE);
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		lapd_dl_release_confirm(sk);
		lapd_stop_t200(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		// MDL-ERROR-INDICATION(C)
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_printk_sk(KERN_ERR, sk,
			"Unexpected UA in state %s\n",
			lapd_state_to_text(lo->state));
	break;
	}

	return FALSE;
}

int lapd_socket_handle_uframe_disc(
	struct sock *sk,
	struct sk_buff *skb)
{
	lapd_debug_sk(sk, "received u-frame DISC\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

	switch (lo->state) {
	case LAPD_DLS_TEI_ASSIGNED:
	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_REESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DM, hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_UA, hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		lapd_dl_release_indication(sk);
		lapd_discard_iqueue(sk);
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_UA, hdr->u.p_f, NULL, 0);
		lapd_stop_t200(sk);
		lapd_stop_t203(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_printk_sk(KERN_ERR, sk,
			"Unexpected DISC in state %s\n",
			lapd_state_to_text(lo->state));
	break;
	}

	return FALSE;
}

int lapd_socket_handle_uframe_dm(
	struct sock *sk,
	struct sk_buff *skb)
{
	lapd_debug_sk(sk, "received u-frame DM\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

	switch (lo->state) {
	case LAPD_DLS_TEI_ASSIGNED:
		if (!hdr->u.p_f && sk->sk_state == TCP_ESTABLISHED) {
			lapd_start_t200(sk);
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
			lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH);
		}
	break;

	case LAPD_DLS_AWAITING_ESTABLISH:
		if (!hdr->u.p_f) {
			lapd_stop_t200(sk);
			lapd_dl_release_indication(sk);
			lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		}
	break;

	case LAPD_DLS_AWAITING_REESTABLISH:
		if (!hdr->u.p_f) {
			lapd_stop_t200(sk);
			lapd_dl_release_indication(sk);
			lapd_discard_iqueue(sk);
			lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		}
	break;

	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		if (!hdr->u.p_f) {
			lapd_stop_t200(sk);
			lapd_dl_release_confirm(sk);
			lapd_discard_iqueue(sk);
			lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		}
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		if (!hdr->u.p_f) {
			lapd_stop_t200(sk);
			lapd_dl_release_confirm(sk);
			lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		}
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		if (hdr->u.p_f) {
			// MDL-ERROR-INDICATION(B)
		} else {
			// MDL-ERROR-INDICATION(E)
		}

		if (lo->in_timer_recovery) {
			lo->retrans_cnt = 0;
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
			lapd_start_t200(sk);
			lapd_change_state(sk, LAPD_DLS_AWAITING_REESTABLISH);
		} else if (!hdr->u.p_f) {
			lo->retrans_cnt = 0;
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);

			if (lo->peer_busy) {
				lapd_start_t200(sk);
			} else {
				lapd_stop_t200(sk);
				lapd_start_t203(sk);
			}

			lapd_change_state(sk, LAPD_DLS_AWAITING_REESTABLISH);
		}
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_printk_sk(KERN_ERR, sk,
			"Unexpected DM in state %s\n",
			lapd_state_to_text(lo->state));
	break;
	}

	return FALSE;
}

int lapd_socket_handle_uframe_sabme(
	struct sock *sk,
	struct sk_buff *skb)
{
	lapd_debug_sk(sk, "received u-frame SABME\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

	switch (lo->state) {

	case LAPD_DLS_TEI_ASSIGNED:
		if (sk->sk_state == TCP_ESTABLISHED) {
			lapd_dl_establish_indication(sk);

			lo->v_s = 0;
			lo->v_r = 0;
			lo->v_a = 0;

			lo->peer_busy = FALSE;
			lo->me_busy = FALSE;
			lo->rejection_exception = FALSE;
			lo->in_timer_recovery = FALSE;

			lapd_start_t203(sk);
			lapd_change_state(sk, LAPD_DLS_LINK_CONNECTION_ESTABLISHED);
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_UA, hdr->u.p_f, NULL, 0);
		} else {
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DM, hdr->u.p_f, NULL, 0);
		}
	break;

	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_REESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_UA, hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DM, hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		// MDL-ERROR-INDICATION(F)

		if (lo->v_s != lo->v_a) {
			lapd_discard_iqueue(sk);
			lapd_dl_establish_indication(sk);
		}

		lo->v_s = 0;
		lo->v_r = 0;
		lo->v_a = 0;

		lo->peer_busy = FALSE;
		lo->me_busy = FALSE;
		lo->rejection_exception = FALSE;
		lo->in_timer_recovery = FALSE;

		lapd_stop_t200(sk);
		lapd_start_t203(sk);
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_UA, hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_printk_sk(KERN_ERR, sk,
			"Unexpected SABME in state %s\n",
			lapd_state_to_text(lo->state));
	break;
	}

	return FALSE;
}

/*

  Add AF_LAPD to linux/include/linux/socket.h
  Add PF_LAPD to linux/include/linux/socket.h
  Add SOL_LAPD to linux/inclode/linx/socket.h ????

  Add ARPHRD_ISDN_DCHAN to linux/include/linux/if_arp.h
  Add ETH_P_LAPD to linux/include/linux/if_ether.h

  TODO: Determine where the information "I am a NT" or "I am a TE" should
  reside (lowlevel driver, socket layer) and how each layer can pass it
  to other layers.
  BRI chipsets must know if they act as NT or TE since it influences how
  the phisycal layer work. PRI cards do not need to know their role since
  they are perfectly symmetrical g.703 endpoints.
  IMHO it should be a card configuration parameter.

*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "tei_mgmt.h"

inline int lapd_send_frame(struct sk_buff *skb)
{
	int err;

	if((err = dev_queue_xmit(skb)) < 0) {
		printk(KERN_ERR "lapd: dev_queue_xmit: %d\n", err);
		kfree_skb(skb);
		return err;
	}

	return skb->len;
}

int lapd_prepare_uframe(struct sk_buff *skb, u8 sapi, u8 tei,
	enum lapd_uframe_function function)
{
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr *hdr =
		(struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = sapi;

	enum lapd_cr cr = COMMAND;
	switch (function) {
		case SABME: cr = COMMAND; break;
		case DM:    cr = RESPONSE; break;
		case UI:    cr = COMMAND; break;
		case DISC:  cr = COMMAND; break;
		case UA:    cr = RESPONSE; break;
		case FRMR:  cr = RESPONSE; break;
		case XID: printk(KERN_ERR "lapd: unsupported XID\n"); break;
	}

	hdr->addr.c_r = ((cr == RESPONSE) == !(skb->skb_dev->flags & IFF_ALLMULTI))?1:0;
	hdr->addr.ea1 = 0;
	hdr->addr.tei = tei;
	hdr->addr.ea2 = 1;

	hdr->control = lapd_uframe_make_control(function, 0/* p_f*/);

	return 0;
}

/*
int lapd_send_uframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_uframe_function function, void *data, int datalen)
{
	int err;

	struct sk_buff *skb =
		lapd_prepare_uframe(sk, sapi, tei,
			function, datalen, &err);

	if (!skb) return err;

	if (data && datalen)
		memcpy(skb_put(skb, datalen), data, datalen);

	return lapd_send_frame(skb);
}
*/

void lapd_prepare_iframe(struct sk_buff *skb, u8 sapi, u8 tei)
{
	// lo reference is ok for iframes

	struct lapd_opt *lo = lapd_sk(sk);

	if (!lo->dev) {
		*err = -ENETUNREACH;
		return NULL;
	}

	if ((lo->v_s - lo->v_a + 128) % 128
	     > lo->k) {
		// We should not trasnmit (see 5.6.1)
	}

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = sapi;
	hdr->addr.c_r = lo->nt_mode ? 1 : 0; // I-frames are always commands
	hdr->addr.ea1 = 0;
	hdr->addr.tei = tei;
	hdr->addr.ea2 = 1;

	return skb;
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

static void lapd_flush_queue(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	struct sk_buff *skb;
	while ((skb = sk->sk_send_head)) {

		u8 drop_simul;
		get_random_bytes(&drop_simul, sizeof(drop_simul));

		if (drop_simul > 127)
			dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));
		else
			printk(KERN_DEBUG "lapd: Simulating frame drop\n");

		sk->sk_send_head = skb->next;

		if (sk->sk_send_head == (struct sk_buff *)&sk->sk_write_queue)
		        sk->sk_send_head = NULL;

		sk_reset_timer(sk, &lo->T200_timer,
			jiffies + lo->T200);

		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		printk(KERN_DEBUG "lapd: transmitted frame %d, send_head=%p\n",
			hdr->i.n_s, sk->sk_send_head);

	}
	
}

static void lapd_retransmit_from(struct sock *sk, int n_s)
{
	struct sk_buff *skb;

	

        sk_stream_for_retrans_queue(skb, sk) {
                struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		printk(KERN_DEBUG
			"Retransmitting frame N(S)=%d\n",
			hdr->i.n_s);

		dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));

		if (hdr->i.n_s == n_s)
			break;
	}
}

static inline void lapd_enter_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = TRUE;

	printk(KERN_DEBUG "lapd: Entering timer recovery condition\n");
}

static inline void lapd_leave_timer_recovery(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lo->in_timer_recovery = FALSE;

	printk(KERN_DEBUG "lapd: Leaving timer recovery condition\n");
}


static void lapd_receive_ack(struct sock *sk, int n_r)
{
	struct lapd_opt *lo = lapd_sk(sk);

	// Nothing to ack
	if (n_r == lo->v_a)
		return;

	// The sender is acking some frame
	printk(KERN_DEBUG "lapd: received ack for frames from %d to %d-1\n",
		lo->v_a,
		n_r);

	struct sk_buff *skb;
	sk_stream_for_retrans_queue(skb, sk) {
		struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;

		__skb_unlink(skb, skb->list);
		sk_stream_free_skb(sk, skb);

		printk(KERN_INFO "lapd: acking frame %d\n",
			hdr->i.n_s);
	}

	lo->v_a = n_r;
}


int lapd_send_completed_uframe(struct sk_buff *skb)
{
	return lapd_send_frame(skb);
}

int lapd_send_completed_iframe(struct sk_buff *skb)
{
	lapd_iframe_queue(skb->sk, skb);
	lapd_flush_queue(skb->sk);

	return 0;
}

int lapd_send_iframe(struct sock *sk, u8 sapi, u8 tei,
	void *data, int datalen)
{
	int err;

	struct sk_buff *skb =
		lapd_prepare_iframe(sk, sapi, tei,
			datalen, &err);

	if (!skb) return err;

	if (data && datalen)
		memcpy(skb_put(skb, datalen), data, datalen);

	lapd_iframe_queue(sk, skb);
	lapd_flush_queue(sk);

	return 0;
}

struct sk_buff *lapd_prepare_sframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_cr c_r,
	enum lapd_sframe_function function, int p_f, int *err)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if (!lo->dev) {
		*err = -ENETUNREACH;
		return NULL;
	}

	struct sk_buff *skb = sock_alloc_send_skb(sk,
			sizeof(struct lapd_hdr_e),
			0, err);
// FIXME		(msg->msg_flags & MSG_DONTWAIT), &err);

	if (!skb) return NULL;

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr_e *hdr =
		(struct lapd_hdr_e *)skb_put(skb, sizeof(struct lapd_hdr_e));

	hdr->addr.sapi = sapi;
	hdr->addr.c_r = lapd_make_cr(lo->nt_mode, c_r);
	hdr->addr.ea1 = 0;
	hdr->addr.tei = tei;
	hdr->addr.ea2 = 1;

	hdr->control  = lapd_sframe_make_control(function);
	hdr->control2 = lapd_sframe_make_control2(lo->v_r, p_f);

	return skb;
}

int lapd_send_sframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_cr c_r,
	enum lapd_uframe_function function, int p_f)
{
	int err;

	struct sk_buff *skb =
		lapd_prepare_sframe(sk, sapi, tei,
			c_r, function, p_f, &err);

	if (!skb) return err;

	return lapd_send_frame(skb);
}

void lapd_multiframe_established(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: Multiple frame mode established\n");

	// If we're called at the arrival of
	// SABME, T200 is already stopped anyway
	sk_stop_timer(sk, &lo->T200_timer);

	lo->status = LINK_CONNECTION_ESTABLISHED;

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
		jiffies + lo->T203);

	// Notify the socket status changed
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);
}

void lapd_multiframe_released(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: Multiple frame mode released\n");

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = LINK_CONNECTION_RELEASED;

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

	printk(KERN_DEBUG "lapd: Starting multiframe establishment\n");

	// FIXME (not only q931)
	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->T200);
	lo->N200_cnt = 0;

	// Other checks

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = AWAITING_ESTABLISH;

	lapd_send_uframe(sk, 0, lo->te.tei, SABME, NULL, 0);
}

void lapd_start_multiframe_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: Starting multiframe release\n");

	skb_queue_purge(&sk->sk_write_queue);

	lo->status = AWAITING_RELEASE;

	lapd_send_uframe(sk, 0, lo->te.tei, DISC, NULL, 0);

	lo->N200_cnt = 0;

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->T200);
}

void lapd_q931_T203_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
//	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: q931 T203\n");

	sock_put(sk);
}

void lapd_x25_T200_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
//	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: x25 T200\n");

	sock_put(sk);
}

void lapd_x25_T203_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
//	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: x25 T203\n");

	sock_put(sk);
}

static void lapd_send_enquiry_response(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->me_busy) {
		lapd_send_sframe(sk, 0, lo->te.tei, RESPONSE, RNR, 1);
	} else {
		lapd_send_sframe(sk, 0, lo->te.tei, RESPONSE, RR, 1);
	}

	lo->peer_waiting_for_ack = FALSE;
}

static void lapd_transmit_enquiry(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->me_busy) {
		lapd_send_sframe(sk, 0, lo->te.tei, COMMAND, RNR, 1);
	} else {
		lapd_send_sframe(sk, 0, lo->te.tei, COMMAND, RR, 1);
	}

	lo->peer_waiting_for_ack = TRUE;

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->T200);
}

static void lapd_start_invalid_nr_recovery(struct sock *sk)
{
	// Signal error to user TODO

	lapd_start_multiframe_establishment(sk);
}

static inline void lapd_handle_socket_iframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: received i-frame\n");
	printk(KERN_DEBUG
		"lapd: V(S)=%u V(R)=%u V(A)=%u: received N(S)=%d N(R)=%d\n",
		lo->v_s,
		lo->v_r,
		lo->v_a,
		hdr->i.n_s,
		hdr->i.n_r);

	if (lo->status != LINK_CONNECTION_ESTABLISHED) {
		printk(KERN_WARNING
			"lapd: "
			"received i-frame while link not established"
			" (status=%d)\n",
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
		if (err < 0) {
			printk(KERN_ERR "lapd: sock_queue_rcv_skb err: %d\n",
				err);

			// What shall we do now?
			// TODO
		}
	} else {
		// uhuh... the frame is not in sequence, we have to recover

		if (lo->rejection_exception) {
			if (hdr->i.p)
				lapd_send_enquiry_response(sk);
		} else {
			lapd_send_sframe(sk, 0, lo->te.tei, RESPONSE, REJ, hdr->i.p);
			lo->rejection_exception = TRUE;
			lo->peer_waiting_for_ack = FALSE;
		}
	}

	if (!lapd_is_valid_nr(lo, hdr->i.n_r)) {
		printk(KERN_WARNING
			"lapd: invalid N(R)=%d (V(s)=%d, V(a)=%d)\n",
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
				jiffies + lo->T203);

		} else if (hdr->i.n_r != lo->v_a) {
			sk_reset_timer(sk, &lo->T200_timer,
				jiffies + lo->T200);
		}
	}

	if (lo->peer_waiting_for_ack) {
		lo->peer_waiting_for_ack = FALSE;
		lapd_send_sframe(sk, 0, lo->te.tei, RESPONSE, RR, 0);
	}
		
}

static inline void lapd_handle_socket_sframe_rr(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received s-frame RR\n");

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	// Always clear peer busy condition
	lo->peer_busy = FALSE;

	if (hdr->s.n_r == lo->v_s) {
		sk_stop_timer(sk, &lo->T200_timer);

		sk_reset_timer(sk, &lo->T203_timer,
			jiffies + lo->T203);
	} else if (hdr->s.n_r != lo->v_a) {
		sk_reset_timer(sk, &lo->T200_timer,
			jiffies + lo->T200);
	}
}

static inline void lapd_handle_socket_sframe_rnr(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received s-frame REJ\n");

	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	// Always set peer busy condition
	lo->peer_busy = TRUE;

	if (lo->v_a != hdr->s.n_r) {
	}

	sk_stop_timer(sk, &lo->T203_timer);

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->T200);
}

static inline void lapd_handle_socket_sframe_rej(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: received s-frame REJ\n");

	// Always clear peer busy condition
	lo->peer_busy = FALSE;

	lapd_retransmit_from(sk, hdr->s.n_r);

	lapd_flush_queue(sk);

	sk_stop_timer(sk, &lo->T200_timer);

	sk_reset_timer(sk, &lo->T203_timer,
		jiffies + lo->T203);
}

static inline void lapd_handle_socket_sframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr_e *hdr = (struct lapd_hdr_e *)skb->h.raw;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: received s-frame\n");

	if (lo->status != LINK_CONNECTION_ESTABLISHED) {
		printk(KERN_WARNING
			"lapd: "
			"received s-frame while link not established"
			" (status=%d)\n",
			lo->status);
		return;
	}

	printk(KERN_DEBUG
		"lapd: V(S)=%u V(R)=%u V(A)=%u: received N(R)=%d\n",
		lo->v_s,
		lo->v_r,
		lo->v_a,
		hdr->s.n_r);

	lapd_receive_ack(sk, hdr->s.n_r);

	if (lapd_is_command(lo->nt_mode,hdr->addr.c_r) && hdr->s.p_f)
		lapd_send_enquiry_response(sk);

	if (!lapd_is_valid_nr(lo, hdr->s.n_r)) {
		printk(KERN_WARNING
			"lapd: invalid N(R)=%d (V(s)=%d, V(a)=%d)\n",
			hdr->s.n_r,
			lo->v_s,
			lo->v_a);

		lapd_start_invalid_nr_recovery(sk);

		return;
	}

	if (lo->in_timer_recovery) {
		if (!lapd_is_command(lo->nt_mode,hdr->addr.c_r) && hdr->s.p_f) {
			lapd_retransmit_from(sk, hdr->s.n_r);
			lapd_leave_timer_recovery(sk);
		}
	}

	switch (lapd_sframe_function(hdr->control)) {
	case RR: lapd_handle_socket_sframe_rr(sk, skb); break;
	case RNR: lapd_handle_socket_sframe_rnr(sk, skb); break;
	case REJ: lapd_handle_socket_sframe_rej(sk, skb); break;
	}

/* UH? What does this do?
                if (skb_queue_len(&st->l2.i_queue) && (typ == RR))
                        st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
*/

}

void lapd_q931_T200_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_opt *lo = lapd_sk(sk);

	printk(KERN_DEBUG "lapd: q931 T200\n");

	if (lo->status == AWAITING_ESTABLISH) {
		lapd_send_uframe(sk, 0, lo->te.tei, SABME, NULL, 0);

		if (lo->N200_cnt > lo->N200) {
			lapd_multiframe_released(sk);

			sk->sk_err = -ETIMEDOUT;

			goto max_count;
		}
	}
	else if (lo->status == AWAITING_RELEASE) {
		lapd_send_uframe(sk, 0, lo->te.tei, DISC, NULL, 0);

		if (lo->N200_cnt > lo->N200) {
			lapd_multiframe_released(sk);

			goto max_count;
		};
	}
	else if (lo->status == LINK_CONNECTION_ESTABLISHED) {
		if (lo->N200_cnt == lo->N200) {
			lapd_start_multiframe_establishment(sk);
		} else {
			if (!lo->in_timer_recovery)
				lapd_enter_timer_recovery(sk);
			else
				lo->N200_cnt++;
			// Here I'd prefer to implement point a) 3 of 5.6.7

			lapd_transmit_enquiry(sk);
		}
	}
	else {
		printk(KERN_ERR
			"lapd: Unexpected T200 in state %d\n",
			lo->status);

		goto err_unexpected_t200;
	}

	lo->N200_cnt++;

	sk_reset_timer(sk, &lo->T200_timer,
		jiffies + lo->T200);

err_unexpected_t200:
max_count:

	sock_put(sk);
}

static inline void lapd_handle_socket_uframe_sabme(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame SABME\n");

	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->nt_mode) {
		if (sk->sk_state == TCP_LISTEN)
			lapd_send_uframe(sk, 0, lo->te.tei, UA, NULL, 0);
		else
			lapd_send_uframe(sk, 0, lo->te.tei, DM, NULL, 0);
	} else {
		if (lo->status == LINK_CONNECTION_RELEASED) {
			lapd_multiframe_established(sk);

			lapd_send_uframe(sk, 0, lo->te.tei, UA, NULL, 0);
		}
		else if (lo->status == LINK_CONNECTION_ESTABLISHED) {
		
		}
		else if (lo->status == AWAITING_ESTABLISH) {
			lapd_send_uframe(sk, 0, lo->te.tei, UA, NULL, 0);
		}
		else if (lo->status == AWAITING_RELEASE) {
			lapd_send_uframe(sk, 0, lo->te.tei, DM, NULL, 0);
		}
		else {
			printk(KERN_ERR
				"lapd: Unexpected UA in wrong state %d\n",
				lo->status);
		}
	}
}

static inline void lapd_handle_socket_uframe_dm(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame DM\n");

	// on receipt of an unsolicited DM response with the F bit set to 0,
	// the data link layer entity shall, if it is able to, initiate the
	// establishment procedures by the transmission of an SABME 
	// (see § 5.5.1.2). Otherwise, the DM shall be ignored;

	lapd_start_multiframe_establishment(sk);
}

static inline void lapd_handle_socket_uframe_ui(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame UI\n");

	skb_pull(skb, sizeof(struct lapd_hdr));

	if (sock_queue_rcv_skb(sk, skb) < 0) {
		// TODO What should we do here?
		return;
	}
}

static inline void lapd_handle_socket_uframe_disc(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame DISC\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;

	if (lo->status == LINK_CONNECTION_RELEASED) {
		lapd_send_uframe(sk, 0, lo->te.tei, DM, NULL, hdr->u.p_f);
	}
	else if (lo->status == LINK_CONNECTION_ESTABLISHED) {
		lapd_multiframe_released(sk);

		lapd_send_uframe(sk, 0, lo->te.tei, UA, NULL, hdr->u.p_f);
	}
	else if (lo->status == AWAITING_ESTABLISH) {
		lapd_send_uframe(sk, 0, lo->te.tei, DM, NULL, 0);
	}
	else if (lo->status == AWAITING_RELEASE) {
		lapd_send_uframe(sk, 0, lo->te.tei, UA, NULL, 0);
	}
	else {
		printk(KERN_ERR
			"lapd: Unexpected UA in wrong state %d\n",
			lo->status);
	}
}

static inline void lapd_handle_socket_uframe_ua(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame UA\n");

	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->status == AWAITING_ESTABLISH) {
		lapd_multiframe_established(sk);
	}
	else if (lo->status == AWAITING_RELEASE) {
		lapd_multiframe_released(sk);
	}
	else if (lo->status == LINK_CONNECTION_RELEASED) {
		// Possible multiple-tei-assignment, tei check?
		return;
	}
	else {
		printk(KERN_ERR
			"lapd: Unexpected UA in wrong state %d\n",
			lo->status);
	}
}

static inline void lapd_handle_socket_uframe_frmr(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame FRMR\n");
}

static inline void lapd_handle_socket_uframe_xid(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame XID\n");
}

static inline void lapd_handle_socket_uframe(struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;

	printk(KERN_DEBUG "lapd: received u-frame\n");

	switch (lapd_uframe_function(hdr->control)) {
	case SABME:
		lapd_handle_socket_uframe_sabme(sk, skb);
	break;

	case DM:
		lapd_handle_socket_uframe_dm(sk, skb);
	break;

	case UI:
		lapd_handle_socket_uframe_ui(sk, skb);
	break;

	case DISC:
		lapd_handle_socket_uframe_disc(sk, skb);
	break;

	case UA:
		lapd_handle_socket_uframe_ua(sk, skb);
	break;

	case FRMR:
		lapd_handle_socket_uframe_frmr(sk, skb);
	break;

	case XID:
		lapd_handle_socket_uframe_xid(sk, skb);
	break;

	default:
		printk(KERN_ERR
			"lapd: received invalid u-frame function %d\n",
			hdr->control);
	}
}

inline int lapd_handle_socket_frame(struct sock *sk, struct sk_buff *skb)
{
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;

	switch (lapd_frame_type(hdr->control)) {
	case IFRAME:
		lapd_handle_socket_iframe(sk, skb);
	break;

	case SFRAME:
		lapd_handle_socket_sframe(sk, skb);
	break;

	case UFRAME:
		lapd_handle_socket_uframe(sk, skb);
	break;
	};

	return 0;
}


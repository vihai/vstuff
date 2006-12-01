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
 * --------------
 *  This file implements point-to-point datalink procedures
 */

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/skbuff.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "input.h"
#include "output.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "datalink.h"
#include "sock_inline.h"

#ifdef SOCK_DEBUGGING
#define lapd_debug_dlc(ls, format, arg...)	\
		SOCK_DEBUG(&(ls)->sk, "lapd: "		\
			"%s "				\
			"V(S)=%u V(R)=%u V(A)=%u: "	\
			format,				\
			(ls)->dev->dev->name,		\
			(ls)->v_s,			\
			(ls)->v_r,			\
			(ls)->v_a,			\
			## arg)
#else
#define lapd_debug_dlc(ls, format, arg...)	\
		do { } while (0)
#endif

#define lapd_msg_dlc(ls, lvl, format, arg...)	\
	printk(lvl "lapd: "			\
		"%s "				\
		"V(S)=%u V(R)=%u V(A)=%u: "	\
		format,				\
		(ls)->dev->dev->name,		\
		(ls)->v_s,			\
		(ls)->v_r,			\
		(ls)->v_a,			\
		## arg)

#define lapd_start_timer(ls, timername)					\
	do {								\
		lapd_debug_dlc(lapd_sock, "%s:%d %s START\n",	\
			__FILE__, __LINE__, #timername);		\
		sk_reset_timer(&(ls)->sk, &(ls)->timer_##timername,	\
			jiffies + (ls)->sap->timername);		\
	} while(0)

#define lapd_stop_timer(ls, timername)					\
	do {								\
		lapd_debug_dlc(lapd_sock, "%s:%d %s STOP\n",	\
			__FILE__, __LINE__, #timername);		\
		sk_stop_timer(&(ls)->sk, &(ls)->timer_##timername);	\
	} while(0)

static const char *lapd_state_to_text(enum lapd_datalink_state state)
{
	switch (state) {
	case LAPD_DLS_NULL:
		return "NULL";
	case LAPD_DLS_LISTENING:
		return "LISTENING";
	case LAPD_DLS_1_TEI_UNASSIGNED:
		return "1_TEI_UNASSIGNED";
	case LAPD_DLS_2_AWAITING_TEI:
		return "2_AWAITING_TEI";
	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		return "3_ESTABLISH_AWAITING_TEI";
	case LAPD_DLS_4_TEI_ASSIGNED:
		return "4_TEI_ASSIGNED";
	case LAPD_DLS_5_AWAITING_ESTABLISH:
		return "5_AWAITING_ESTABLISH";
	case LAPD_DLS_6_AWAITING_RELEASE:
		return "6_AWAITING_RELEASE";
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		return "7_LINK_CONNECTION_ESTABLISHED";
	case LAPD_DLS_8_TIMER_RECOVERY:
		return "8_TIMER_RECOVERY";
	}

	return NULL;
}

static void lapd_change_state(
	struct lapd_sock *lapd_sock,
	enum lapd_datalink_state newstate)
{
	const char *oldstate;
	oldstate = lapd_state_to_text(lapd_sock->state);

	lapd_sock->state = newstate;

	if (!sock_flag(&lapd_sock->sk, SOCK_DEAD))
		lapd_sock->sk.sk_state_change(&lapd_sock->sk);

	lapd_debug_ls(lapd_sock, "Changed state from %s to %s\n",
		oldstate, lapd_state_to_text(lapd_sock->state));
}

#define lapd_unexpected_primitive(lapd_sock)	\
	_lapd_unexpected_primitive((lapd_sock), __FUNCTION__)

static void _lapd_unexpected_primitive(
	struct lapd_sock *lapd_sock,
	const char *event)
{
	lapd_msg_ls(lapd_sock, KERN_ERR,
		"Unexpected %s in state %s\n",
		event,
		lapd_state_to_text(lapd_sock->state));
}

#define lapd_unexpected_message(lapd_sock)	\
	_lapd_unexpected_message((lapd_sock), __FUNCTION__)

static void _lapd_unexpected_message(
	struct lapd_sock *lapd_sock,
	const char *event)
{
	lapd_msg_ls(lapd_sock, KERN_ERR,
		"Unexpected %s in state %s\n",
		event,
		lapd_state_to_text(lapd_sock->state));
}

#define lapd_unexpected_timer(lapd_sock)	\
	_lapd_unexpected_timer((lapd_sock), __FUNCTION__)

static void _lapd_unexpected_timer(
	struct lapd_sock *lapd_sock,
	const char *event)
{
	lapd_msg_ls(lapd_sock, KERN_ERR,
		"Unexpected %s in state %s\n",
		event,
		lapd_state_to_text(lapd_sock->state));
}

void lapd_mdl_primitive(
	struct lapd_sock *lapd_sock,
	enum lapd_mdl_primitive_type type,
	int param)
{
	struct sk_buff *skb;
	struct lapd_dl_primitive *pri;

	skb = alloc_skb(sizeof(struct lapd_dl_primitive), GFP_ATOMIC);
	if (!skb) {
		lapd_msg(KERN_ERR,
			"Cannot queue primitive %d to socket\n",
			type);

		return;
	}

	skb->dev = NULL;

	pri = (struct lapd_dl_primitive *)
		skb_put(skb, sizeof(struct lapd_dl_primitive));
	pri->type = type;
	pri->param = param;

	if (sock_owned_by_user(&lapd_sock->sk)) {
		sk_add_backlog(&lapd_sock->sk, skb);
	} else {
		if (!lapd_dlc_recv(lapd_sock, skb))
			kfree_skb(skb);
	}
}

static void lapd_clear_exception_conditions_procedure(
	struct lapd_sock *lapd_sock)
{
	lapd_sock->own_receiver_busy = FALSE;
	lapd_sock->peer_receiver_busy = FALSE;
	lapd_sock->reject_exception = FALSE;
	lapd_sock->acknowledge_pending = FALSE;
}

static void lapd_establish_datalink_procedure(
	struct lapd_sock *lapd_sock)
{
	lapd_clear_exception_conditions_procedure(lapd_sock);

	lapd_sock->retrans_cnt = 0;

	lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);

	lapd_stop_timer(lapd_sock, T203);
	lapd_start_timer(lapd_sock, T200);
}

static int lapd_prepare_sframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb,
	enum lapd_cr c_r,
	enum lapd_sframe_function function, int p_f)
{
	struct lapd_data_hdr_e *hdr;

	hdr = (struct lapd_data_hdr_e *)
		skb_put(skb, sizeof(struct lapd_data_hdr_e));

	hdr->addr.sapi = lapd_sock->sapi;
	hdr->addr.c_r = lapd_make_cr(lapd_sock->dev, c_r);
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lapd_sock->tei;
	hdr->addr.ea2 = 1;

	hdr->control  = lapd_sframe_make_control(function);
	hdr->control2 = lapd_sframe_make_control2(lapd_sock->v_r, p_f);

	return 0;
}

static int lapd_send_sframe(
	struct lapd_sock *lapd_sock,
	enum lapd_cr c_r,
	enum lapd_uframe_function function, int p_f)
{
	int err = 0;

	struct sk_buff *skb;
	struct lapd_data_hdr_e *hdr;

	skb = lapd_alloc_data_request_skb(lapd_sock->dev,
				sizeof(struct lapd_data_hdr_e));
	if (!skb) {
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	err = lapd_prepare_sframe(lapd_sock, skb,
			c_r, function, p_f);
	if (err < 0) {
		goto err_prepare_sframe;
	}

	hdr = (struct lapd_data_hdr_e *)skb->data;

	lapd_debug_dlc(lapd_sock,
		"Transmitting s-frame %s N(R)=%d\n",
		lapd_sframe_function_name(lapd_sframe_function(hdr->control)),
		lapd_sock->v_r);

	return lapd_ph_data_request(skb);

err_prepare_sframe:
	kfree_skb(skb);
err_alloc_skb:
	return err;
}

static void lapd_transmit_enquiry_procedure(
	struct lapd_sock *lapd_sock)
{
	if (lapd_sock->own_receiver_busy) {
		lapd_send_sframe(lapd_sock,
			LAPD_COMMAND,
			LAPD_SFRAME_FUNC_RNR, 1);
	} else {
		lapd_send_sframe(lapd_sock,
			LAPD_COMMAND,
			LAPD_SFRAME_FUNC_RR, 1);
	}

	lapd_sock->acknowledge_pending = FALSE;
	lapd_start_timer(lapd_sock, T200);
}

static void lapd_nr_error_recovery_procedure(
	struct lapd_sock *lapd_sock)
{
	lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
		LAPD_MDL_ERROR_INDICATION_J);

	lapd_establish_datalink_procedure(lapd_sock);

	lapd_sock->layer_3_initiated = FALSE;
}

static void lapd_enquiry_response_procedure(
	struct lapd_sock *lapd_sock)
{
	if (lapd_sock->own_receiver_busy) {
		lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_RNR, 1);
	} else {
		lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
				LAPD_SFRAME_FUNC_RR, 1);
	}

	lapd_sock->acknowledge_pending = FALSE;
}

static inline int lapd_is_valid_nr(struct lapd_sock *lapd_sock, int n_r)
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

	return (n_r - lapd_sock->v_a + 128) % 128
			<= (lapd_sock->v_s - lapd_sock->v_a + 128) % 128;
}

static void lapd_run_ui_queue(struct lapd_sock *lapd_sock)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&lapd_sock->u_queue)))
		lapd_ph_data_request(skb);
}

static inline void lapd_discard_ui_queue(struct lapd_sock *lapd_sock)
{
	skb_queue_purge(&lapd_sock->u_queue);
}

static inline void lapd_discard_i_queue(struct lapd_sock *lapd_sock)
{
	skb_queue_purge(&lapd_sock->sk.sk_write_queue);
}

static void lapd_run_i_queue(struct lapd_sock *lapd_sock)
{
	struct sock *sk = &lapd_sock->sk;
	struct sk_buff *skb;

	if (lapd_sock->state != LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED)
		return;

	if (lapd_sock->peer_receiver_busy)
		return;

	for (skb = sk->sk_send_head;
	     skb &&
	       skb != (struct sk_buff *)&sk->sk_write_queue &&
	       lapd_sock->v_s != (lapd_sock->v_a + lapd_sock->sap->k) % 128;
	     skb = skb->next, sk->sk_send_head = skb) {

		struct lapd_data_hdr_e *hdr =
			(struct lapd_data_hdr_e *)skb->data;

		BUG_ON(!hdr);

		hdr->i.n_s = lapd_sock->v_s;
		hdr->i.n_r = lapd_sock->v_r;

		lapd_debug_dlc(lapd_sock,
			"Transmitting i-frame N(S)=%d\n",
			hdr->i.n_s);

		if (!timer_pending(&lapd_sock->timer_T200)) {
			lapd_start_timer(lapd_sock, T200);
			lapd_stop_timer(lapd_sock, T203);
		}

		/* We need to copy the datagram because we will
		 * change N(S) and N(R) in the future
		 */
		lapd_ph_data_request(skb_copy(skb, GFP_ATOMIC));

		lapd_sock->v_s = (lapd_sock->v_s + 1) % 128;
	}

	if (lapd_sock->v_s == (lapd_sock->v_a + lapd_sock->sap->k) % 128)
		lapd_debug_dlc(lapd_sock,
			"k reached, not sending more frames\n");

	if (sk->sk_send_head ==
	    (struct sk_buff *)&sk->sk_write_queue)
		sk->sk_send_head = NULL;
}

static void lapd_invoke_retransmission_procedure(
	struct lapd_sock *lapd_sock)
{
	struct sock *sk = &lapd_sock->sk;

	if (!skb_queue_empty(&sk->sk_write_queue))
		sk->sk_send_head = sk->sk_write_queue.next;

	lapd_sock->v_s = lapd_sock->v_a;

	lapd_run_i_queue(lapd_sock);
}

void lapd_mdl_assign_request(struct lapd_sock *lapd_sock, int tei)
{
	lapd_debug_ls(lapd_sock, "MDL-ASSIGN-REQUEST\n");

	switch(lapd_sock->state) {
	case LAPD_DLS_1_TEI_UNASSIGNED:
	case LAPD_DLS_2_AWAITING_TEI:
		lapd_sock->tei = tei;
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
	break;

	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		lapd_sock->tei = tei;
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_sock->layer_3_initiated = TRUE;
		lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	break;
	}
}

void lapd_mdl_remove_request(struct lapd_sock *lapd_sock)
{
	lapd_debug_ls(lapd_sock, "MDL-REMOVE-REQUEST\n");

	switch(lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		lapd_discard_ui_queue(lapd_sock);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_CONFIRM, 0);
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_stop_timer(lapd_sock, T203);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	break;
	}
}

void lapd_mdl_error_response(struct lapd_sock *lapd_sock)
{
	lapd_debug_ls(lapd_sock, "MDL-ERROR-RESPONSE\n");

	switch(lapd_sock->state) {
	case LAPD_DLS_2_AWAITING_TEI:
		lapd_discard_ui_queue(lapd_sock);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		lapd_discard_ui_queue(lapd_sock);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	break;
	}
}

void lapd_persistent_deactivation(struct lapd_sock *lapd_sock)
{
	lapd_debug_ls(lapd_sock, "PERSISTENT DEACTIVATION\n");

	switch(lapd_sock->state) {
	case LAPD_DLS_1_TEI_UNASSIGNED:
		/* Do nothing */
	break;

	case LAPD_DLS_2_AWAITING_TEI:
		lapd_discard_ui_queue(lapd_sock);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		lapd_discard_ui_queue(lapd_sock);
		lapd_change_state(lapd_sock, LAPD_DLS_1_TEI_UNASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_4_TEI_ASSIGNED:
		lapd_discard_ui_queue(lapd_sock);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_CONFIRM, 0);
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_stop_timer(lapd_sock, T203);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_discard_i_queue(lapd_sock);
		lapd_discard_ui_queue(lapd_sock);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	break;
	}
}

#if 0
static void lapd_dump_queue(struct lapd_sock *lapd_sock)
{
	lapd_debug_dlc(lapd_sock, "vvvvvvvvvvvvvvvvvvvvvvv\n");

	struct sock *sk = &lapd_sock->sk;
	struct sk_buff *skb;
	for (skb = sk->sk_write_queue.next;
	     (skb != (struct sk_buff *)&sk->sk_write_queue);
	     skb = skb->next) {

		lapd_debug_dlc(lapd_sock, "");

		if (sk->sk_send_head)
			printk("HEAD ");

		struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;
		printk("V(S) = %d\n", hdr->i.n_s);
	}

	lapd_debug_dlc(lapd_sock, "^^^^^^^^^^^^^^^^^^^^^^^\n");
}
#endif

static void lapd_ack_frames(struct lapd_sock *lapd_sock, int n_r)
{
	struct sock *sk = &lapd_sock->sk;
	struct sk_buff *skb;

	/* Nothing to ack */
	if (n_r == lapd_sock->v_a)
		return;

	/* The sender is acking some frame */
	lapd_debug_dlc(lapd_sock,
		"received ack for frames from %d to %d-1\n",
		lapd_sock->v_a,
		n_r);

	for (skb = sk->sk_write_queue.next;
	    (skb != (struct sk_buff *)&sk->sk_write_queue) &&
	     skb != sk->sk_send_head;) {
		struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;
		struct sk_buff *old_skb;

		if (hdr->i.n_s == n_r) break;

		lapd_debug_dlc(lapd_sock,
			"peer acked frame %d\n",
			hdr->i.n_s);

		old_skb = skb;

		skb = skb->next;

		__skb_unlink(old_skb, &sk->sk_write_queue);
		__kfree_skb(old_skb);
	}
}

int lapd_prepare_iframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr;

	if ((lapd_sock->v_s - lapd_sock->v_a + 128) % 128
	     > lapd_sock->sap->k) {
		/* We should not trasnmit (see 5.6.1) */
	}

	hdr = (struct lapd_data_hdr_e *)
		skb_put(skb, sizeof(struct lapd_data_hdr_e));

	hdr->addr.sapi = lapd_sock->sapi;
	/* I-frames are always commands */
	hdr->addr.c_r = lapd_make_cr(lapd_sock->dev, LAPD_COMMAND);
	hdr->addr.ea1 = 0;
	hdr->addr.tei = lapd_sock->tei;
	hdr->addr.ea2 = 1;

	hdr->i.ft = 0;
	hdr->i.p = 0;

	return 0;
}

static void lapd_set_own_receiver_busy(
	struct lapd_sock *lapd_sock)
{

	switch (lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_8_TIMER_RECOVERY:
		if (!lapd_sock->own_receiver_busy) {
			lapd_sock->own_receiver_busy = TRUE;
			lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
					LAPD_SFRAME_FUNC_RNR, 0);
			lapd_sock->acknowledge_pending = FALSE;
		}
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	}
}

static void lapd_clear_own_receiver_busy(
	struct lapd_sock *lapd_sock)
{

	switch (lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_8_TIMER_RECOVERY:
		if (lapd_sock->own_receiver_busy) {
			lapd_sock->own_receiver_busy = FALSE;
			lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
					LAPD_SFRAME_FUNC_RR, 0);
			lapd_sock->acknowledge_pending = FALSE;
		}
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
	}
}

static int lapd_sock_queue_iframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	lapd_debug_dlc(lapd_sock,
		"Queueing i-frame\n");

	skb->dev = lapd_sock->dev->dev;

	skb_queue_tail(&lapd_sock->sk.sk_write_queue, skb);

	if (!lapd_sock->sk.sk_send_head)
		lapd_sock->sk.sk_send_head = skb;

	return 0;
}

static void lapd_frame_reject(
	struct lapd_sock *lapd_sock,
	struct sk_buff *rskb,
	enum lapd_format_errors error)
{
	switch (error == LAPD_FE_LENGTH) {
	case LAPD_FE_LENGTH:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_N);
		/* w=1, x=1, y=0, z=0 */
	break;

	case LAPD_FE_N201:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_O);
		/* 0, 0, 1, 0 */
	break;

	case LAPD_FE_UNDEFINED_COMMAND:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_L);
	break;

	case LAPD_FE_I_FIELD_NOT_PERMITTED:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_M);
	break;
	}

	if (lapd_sock->state == LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED ||
	    lapd_sock->state == LAPD_DLS_8_TIMER_RECOVERY) {
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_sock->layer_3_initiated = FALSE;
		lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);
	}

/*

A FRMR-response shall not be generated by a data link layer entity;
however, on receipt of this frame actions according to subclause 5.8.6
of this ETS shall be taken.
**********************************************
	struct sk_buff *skb;
	skb = alloc_skb(
		sizeof(struct lapd_data_hdr) + sizeof(struct lapd_frmr),
		GFP_ATOMIC);
	if (!skb)
		return;

	if (lapd_prepare_uframe(sk, skb, LAPD_UFRAME_FUNC_FRMR, 0) < 0)
		return;

	struct lapd_frmr *frmr =
		(struct lapd_frmr *)skb_put(skb, sizeof(struct lapd_frmr));
	memset(frmr, 0x00, sizeof(struct lapd_frmr));

	struct lapd_data_hdr *rhdr = (struct lapd_data_hdr *)rskb->data;
	struct lapd_data_hdr_e *rhdr_e = (struct lapd_data_hdr_e *)rskb->data;

	switch (lapd_frame_type(rhdr->control)) {
	case LAPD_FRAME_TYPE_IFRAME:
	case LAPD_FRAME_TYPE_SFRAME:
		frmr->control = rhdr_e->control;
		frmr->control2 = rhdr_e->control2;

	case LAPD_FRAME_TYPE_UFRAME:
		frmr->control = 0x00;
		frmr->control2 = rhdr->control;
	break;
	};

	frmr->v_s = lapd_sock->v_s;
	frmr->c_r = lapd_rx_is_response(lapd_sock->dev, rhdr->addr.c_r) ? 1 : 0;
	frmr->v_r = lapd_sock->v_r;

	frmr->z = z;
	frmr->y = y;
	frmr->x = x;
	frmr->w = w;

	lapd_sock_queue_uframe(skb);
*/
}

static int lapd_socket_handle_iframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;
	int queued = FALSE;

	lapd_debug_dlc(lapd_sock,
		"Received i-frame N(S)=%d N(R)=%d\n",
		hdr->i.n_s,
		hdr->i.n_r);

	if (skb->len > lapd_sock->dev->dev->mtu) {
		lapd_msg_ls(lapd_sock, KERN_WARNING,
			"received i-frame longer than mtu (%d > %d),"
			" rejecting\n",
			skb->len,
			lapd_sock->dev->dev->mtu);

		lapd_frame_reject(lapd_sock, skb, LAPD_FE_N201);

		return FALSE;
	}

	/* Move this in af_lapd.c:recvmsg ?? */
	if (atomic_read(&lapd_sock->sk.sk_rmem_alloc) <
		(unsigned)lapd_sock->sk.sk_rcvbuf &&
	    lapd_sock->own_receiver_busy) {

		lapd_debug_dlc(lapd_sock,
			"Input queue not full anymore,"
			" exiting busy condition\n");

		lapd_clear_own_receiver_busy(lapd_sock);
	}

	/* The receive queue is full */
	if (atomic_read(&lapd_sock->sk.sk_rmem_alloc) >=
	    (unsigned)lapd_sock->sk.sk_rcvbuf) {

		lapd_debug_dlc(lapd_sock,
			"Incoming queue full, entering busy condition\n");

		lapd_set_own_receiver_busy(lapd_sock);

		return FALSE;
	}

	switch(lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		if (lapd_sock->own_receiver_busy) {
			if (hdr->i.p) {
				lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
					LAPD_SFRAME_FUNC_RNR, 1);

				lapd_sock->acknowledge_pending = FALSE;
			}
		} else {
			if (hdr->i.n_s == lapd_sock->v_r) {
				lapd_sock->v_r = (lapd_sock->v_r + 1) % 128;
				lapd_sock->reject_exception = FALSE;
				lapd_dl_data_indication(lapd_sock, skb);
				queued = TRUE;

				if (hdr->i.p) {
					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 1);

					lapd_sock->acknowledge_pending = FALSE;
				} else {
					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 0);

/*	FIGURE OUT THIS ACKNOWLEDGE_PENDING STUFF
					if (!lapd_sock->acknowledge_pending) {

						lapd_sock->acknowledge_pending
								= TRUE;
					}
*/
				}
			} else {
				if (lapd_sock->reject_exception) {
					if (hdr->i.p) {
						lapd_send_sframe(lapd_sock,
							LAPD_RESPONSE,
							LAPD_SFRAME_FUNC_RR,
							1);

						lapd_sock->acknowledge_pending
								= FALSE;
					}
				} else {
					lapd_sock->reject_exception = TRUE;

					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_REJ, hdr->i.p);

					lapd_sock->acknowledge_pending
							= FALSE;
				}
			}
		}

		if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
			if (lapd_sock->peer_receiver_busy) {
				lapd_ack_frames(lapd_sock, hdr->s.n_r);
				lapd_sock->v_a = hdr->s.n_r;
			} else {
				if (hdr->s.n_r == lapd_sock->v_s) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;

					lapd_stop_timer(lapd_sock, T200);
					lapd_start_timer(lapd_sock, T203);
				} else {
					if (hdr->s.n_r != lapd_sock->v_a) {
						lapd_ack_frames(lapd_sock,
								hdr->s.n_r);
						lapd_sock->v_a = hdr->s.n_r;
						lapd_start_timer(lapd_sock,
									T200);
					}
				}
			}
		} else {
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);

			lapd_nr_error_recovery_procedure(lapd_sock);
		}
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		if (lapd_sock->own_receiver_busy) {
			if (hdr->i.p) {
				lapd_send_sframe(lapd_sock, LAPD_RESPONSE,
					LAPD_SFRAME_FUNC_RNR, 1);

				lapd_sock->acknowledge_pending = FALSE;
			}
		} else {
			if (hdr->i.n_s == lapd_sock->v_r) {
				lapd_sock->v_r = (lapd_sock->v_r + 1) % 128;
				lapd_sock->reject_exception = FALSE;
				lapd_dl_data_indication(lapd_sock, skb);
				queued = TRUE;

				if (hdr->i.p) {
					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 1);

					lapd_sock->acknowledge_pending = FALSE;
				} else {
					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_RR, 0);

/*	FIGURE OUT THIS ACKNOWLEDGE_PENDING STUFF
					if (!lapd_sock->acknowledge_pending) {

						lapd_sock->acknowledge_pending = TRUE;
					}
*/
				}
			} else {
				if (lapd_sock->reject_exception) {
					if (hdr->i.p) {
						lapd_send_sframe(lapd_sock,
							LAPD_RESPONSE,
							LAPD_SFRAME_FUNC_RR, 1);

						lapd_sock->acknowledge_pending
								= FALSE;
					}
				} else {
					lapd_sock->reject_exception = TRUE;

					lapd_send_sframe(lapd_sock,
						LAPD_RESPONSE,
						LAPD_SFRAME_FUNC_REJ, hdr->i.p);
				}
			}
		}

		if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
			lapd_ack_frames(lapd_sock, hdr->s.n_r);
			lapd_sock->v_a = hdr->s.n_r;
		} else {
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);

			lapd_nr_error_recovery_procedure(lapd_sock);
		}
	break;

	default:
		/* Ignore message */
	break;
	}

	return queued;
}

static int lapd_socket_handle_sframe_rr(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;

	switch(lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_sock->peer_receiver_busy = FALSE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
					LAPD_MDL_ERROR_INDICATION_A);
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}
		}

		if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
			if (hdr->s.n_r == lapd_sock->v_s) {
				lapd_ack_frames(lapd_sock, hdr->s.n_r);
				lapd_sock->v_a = hdr->s.n_r;

				lapd_stop_timer(lapd_sock, T200);
				lapd_start_timer(lapd_sock, T203);
			} else {
				if (hdr->s.n_r != lapd_sock->v_a) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;
					lapd_start_timer(lapd_sock, T200);
				}
			}
		} else {
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
			lapd_nr_error_recovery_procedure(lapd_sock);
		}
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_sock->peer_receiver_busy = FALSE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;

					lapd_stop_timer(lapd_sock, T200);
					lapd_start_timer(lapd_sock, T203);

					lapd_change_state(lapd_sock,
						LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED);
					lapd_invoke_retransmission_procedure(
								lapd_sock);

				} else {
					lapd_nr_error_recovery_procedure(
						lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			} else {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;
				} else {
					lapd_nr_error_recovery_procedure(	
								lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}

			if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
				lapd_ack_frames(lapd_sock, hdr->s.n_r);
				lapd_sock->v_a = hdr->s.n_r;
			} else {
				lapd_nr_error_recovery_procedure(lapd_sock);
				lapd_change_state(lapd_sock,
					LAPD_DLS_5_AWAITING_ESTABLISH);
			}
		}
	break;

	default:
		/* Ignore message */
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_sframe_rnr(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;

	switch(lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_sock->peer_receiver_busy = TRUE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
					LAPD_MDL_ERROR_INDICATION_A);
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}
		}

		if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
			lapd_ack_frames(lapd_sock, hdr->s.n_r);
			lapd_sock->v_a = hdr->s.n_r;

			lapd_invoke_retransmission_procedure(lapd_sock);

			lapd_stop_timer(lapd_sock, T203);
			lapd_start_timer(lapd_sock, T200);
		} else {
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
			lapd_nr_error_recovery_procedure(lapd_sock);
		}
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_sock->peer_receiver_busy = TRUE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;

					lapd_start_timer(lapd_sock, T200);

					lapd_change_state(lapd_sock,
						LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED);
					lapd_invoke_retransmission_procedure(
								lapd_sock);
				} else {
					lapd_nr_error_recovery_procedure(lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			} else {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;
				} else {
					lapd_nr_error_recovery_procedure(
								lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}

			if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
				lapd_ack_frames(lapd_sock, hdr->s.n_r);
				lapd_sock->v_a = hdr->s.n_r;
			} else {
				lapd_nr_error_recovery_procedure(lapd_sock);
				lapd_change_state(lapd_sock,
					LAPD_DLS_5_AWAITING_ESTABLISH);
			}
		}
	break;

	default:
		/* Ignore message */
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_sframe_rej(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;

	switch(lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_sock->peer_receiver_busy = FALSE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
					LAPD_MDL_ERROR_INDICATION_A);
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}
		}

		if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
			lapd_ack_frames(lapd_sock, hdr->s.n_r);
			lapd_sock->v_a = hdr->s.n_r;

			lapd_stop_timer(lapd_sock, T200);
			lapd_start_timer(lapd_sock, T203);

			lapd_invoke_retransmission_procedure(lapd_sock);
		} else {
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
			lapd_nr_error_recovery_procedure(lapd_sock);
		}
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_sock->peer_receiver_busy = FALSE;

		if (lapd_rx_is_response(lapd_sock->dev, hdr->addr.c_r)) {
			if (hdr->s.p_f) {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;

					lapd_stop_timer(lapd_sock, T200);
					lapd_start_timer(lapd_sock, T203);

					lapd_change_state(lapd_sock,
						LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED);

					lapd_invoke_retransmission_procedure(
								lapd_sock);

				} else {
					lapd_nr_error_recovery_procedure(
								lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			} else {
				if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
					lapd_ack_frames(lapd_sock, hdr->s.n_r);
					lapd_sock->v_a = hdr->s.n_r;
				} else {
					lapd_nr_error_recovery_procedure(
								lapd_sock);
					lapd_change_state(lapd_sock,
						LAPD_DLS_5_AWAITING_ESTABLISH);
				}
			}
		} else {
			if (hdr->s.p_f) {
				lapd_enquiry_response_procedure(lapd_sock);
			}

			if (lapd_is_valid_nr(lapd_sock, hdr->s.n_r)) {
				lapd_ack_frames(lapd_sock, hdr->s.n_r);
				lapd_sock->v_a = hdr->s.n_r;
			} else {
				lapd_nr_error_recovery_procedure(lapd_sock);
				lapd_change_state(lapd_sock,
					LAPD_DLS_5_AWAITING_ESTABLISH);
			}
		}
	break;

	default:
		/* Ignore message */
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_sframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr_e *hdr = (struct lapd_data_hdr_e *)skb->data;
	int queued = FALSE;

	lapd_debug_dlc(lapd_sock,
		"Received s-frame %s N(R)=%d\n",
		lapd_sframe_function_name(lapd_sframe_function(hdr->control)),
		hdr->s.n_r);

	if (skb->len != 6) {
		lapd_debug_dlc(lapd_sock,
			"received s-frame with wrong size (%d), rejecting\n",
			skb->len);

		lapd_frame_reject(lapd_sock, skb, LAPD_FE_LENGTH);

		return FALSE;
	}

	switch (lapd_sframe_function(hdr->control)) {
	case LAPD_SFRAME_FUNC_RR:
		queued = lapd_socket_handle_sframe_rr(lapd_sock, skb);
	break;

	case LAPD_SFRAME_FUNC_RNR:
		queued = lapd_socket_handle_sframe_rnr(lapd_sock, skb);
	break;

	case LAPD_SFRAME_FUNC_REJ:
		queued = lapd_socket_handle_sframe_rej(lapd_sock, skb);
	break;

	case LAPD_SFRAME_FUNC_INVALID:
		/* lapd_frame_reject(sk, skb, 1, 0, 0, 0); */
		queued = FALSE;
	break;
	}

	return queued;
}

static int lapd_socket_handle_uframe_ua(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;

	lapd_debug_ls(lapd_sock, "received u-frame UA\n");

	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_C |
			LAPD_MDL_ERROR_INDICATION_D);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		if (!hdr->u.p_f) {
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_D);
		} else {
			lapd_stop_timer(lapd_sock, T200);
			lapd_start_timer(lapd_sock, T203);

			lapd_sock->v_s = 0;
			lapd_sock->v_r = 0;
			lapd_sock->v_a = 0;

			lapd_change_state(lapd_sock,
				LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED);

			if (lapd_sock->layer_3_initiated) {
				lapd_dl_primitive(lapd_sock,
					LAPD_DL_ESTABLISH_CONFIRM, 0);
				lapd_run_i_queue(lapd_sock);
			} else {
				if (lapd_sock->v_s != lapd_sock->v_a) {
					lapd_discard_i_queue(lapd_sock);
					lapd_dl_primitive(lapd_sock,
						LAPD_DL_ESTABLISH_INDICATION,
						0);
				}
			}
		}
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		if (!hdr->u.p_f) {
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_D);
		} else {
			lapd_stop_timer(lapd_sock, T200);
			lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_RELEASE_CONFIRM, 0);
		}
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_C |
			LAPD_MDL_ERROR_INDICATION_D);
	break;

	default:
		lapd_unexpected_message(lapd_sock);
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_uframe_disc(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;

	lapd_debug_ls(lapd_sock, "received u-frame DISC\n");

	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
	case LAPD_DLS_5_AWAITING_ESTABLISH:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DM,
				hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
				hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_i_queue(lapd_sock);
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
				hdr->u.p_f, NULL, 0);
		lapd_stop_timer(lapd_sock, T200);
		lapd_stop_timer(lapd_sock, T203);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_discard_i_queue(lapd_sock);
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
				hdr->u.p_f, NULL, 0);
		lapd_stop_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_INDICATION, 0);
	break;

	default:
		lapd_unexpected_message(lapd_sock);
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_uframe_frmr(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	lapd_debug_ls(lapd_sock, "received u-frame FRMR\n");

/*
	struct lapd_frmr *frmr =
		(struct lapd_frmr *)(skb->data + sizeof(struct lapd_data_hdr));

	if ((frmr->control == 0 &&
	     lapd_uframe_function(frmr->control2) == LAPD_UFRAME_FUNC_UA) ||
	    (frmr->control != 0 &&
  	      (lapd_frame_type(frmr->control) == LAPD_FRAME_TYPE_IFRAME ||
	       lapd_frame_type(frmr->control) == LAPD_FRAME_TYPE_SFRAME))) {
*/

	lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
		LAPD_MDL_ERROR_INDICATION_K);

	lapd_establish_datalink_procedure(lapd_sock);

	lapd_sock->layer_3_initiated = FALSE;

	lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);

	return FALSE;
}

static int lapd_socket_handle_uframe_dm(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;

	lapd_debug_ls(lapd_sock, "received u-frame DM\n");

	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		if (!hdr->u.p_f &&
		    lapd_sock->sk.sk_state == LAPD_SK_STATE_NORMAL_DLC) {

			lapd_establish_datalink_procedure(lapd_sock);
			lapd_sock->layer_3_initiated = TRUE;
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
		}
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		if (hdr->u.p_f) {
			lapd_discard_i_queue(lapd_sock);
			lapd_stop_timer(lapd_sock, T200);
			lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_RELEASE_INDICATION, 0);
		}
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		if (hdr->u.p_f) {
			lapd_stop_timer(lapd_sock, T200);
			lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_RELEASE_CONFIRM, 0);
		}
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_8_TIMER_RECOVERY:
		if (hdr->u.p_f) {
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_B);
		} else {
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_E);

			lapd_establish_datalink_procedure(lapd_sock);
			lapd_sock->layer_3_initiated = FALSE;
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
		}
	break;

	default:
		lapd_unexpected_message(lapd_sock);
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_uframe_sabme(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;

	lapd_debug_ls(lapd_sock, "received u-frame SABME\n");

	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		if (lapd_sock->sk.sk_state == LAPD_SK_STATE_NORMAL_DLC) {
			lapd_clear_exception_conditions_procedure(lapd_sock);

			lapd_sock->v_s = 0;
			lapd_sock->v_r = 0;
			lapd_sock->v_a = 0;

			lapd_start_timer(lapd_sock, T203);
			lapd_change_state(lapd_sock,
				LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED);

			lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
					hdr->u.p_f, NULL, 0);

			lapd_dl_primitive(lapd_sock,
				LAPD_DL_ESTABLISH_INDICATION, 0);
		} else {
			lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DM,
					hdr->u.p_f, NULL, 0);
		}
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
					hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DM,
					hdr->u.p_f, NULL, 0);
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
					hdr->u.p_f, NULL, 0);

		lapd_clear_exception_conditions_procedure(lapd_sock);

		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_F);

		lapd_stop_timer(lapd_sock, T200);
		lapd_start_timer(lapd_sock, T203);

		lapd_sock->v_s = 0;
		lapd_sock->v_r = 0;
		lapd_sock->v_a = 0;

		if (lapd_sock->v_s != lapd_sock->v_a) {
			lapd_discard_i_queue(lapd_sock);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_ESTABLISH_INDICATION, 0);
		}
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_UA,
				hdr->u.p_f, NULL, 0);

		lapd_clear_exception_conditions_procedure(lapd_sock);

		lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
			LAPD_MDL_ERROR_INDICATION_F);

		lapd_stop_timer(lapd_sock, T200);
		lapd_start_timer(lapd_sock, T203);

		lapd_sock->v_s = 0;
		lapd_sock->v_r = 0;
		lapd_sock->v_a = 0;

		if (lapd_sock->v_s != lapd_sock->v_a) {
			lapd_discard_i_queue(lapd_sock);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_ESTABLISH_INDICATION, 0);
		}
	break;

	default:
		lapd_unexpected_message(lapd_sock);
	break;
	}

	return FALSE;
}

static int lapd_socket_handle_uframe_ui(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	lapd_debug_ls(lapd_sock, "received u-frame UI\n");

	/* If state < 4 signal error */

	return lapd_dl_unit_data_indication(lapd_sock, skb);
}

static int lapd_socket_handle_uframe_xid(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	lapd_debug_ls(lapd_sock, "received u-frame XID\n");

	/* Should we reject it? */

	return FALSE;
}

static int lapd_socket_handle_uframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;
	int queued = FALSE;

	lapd_debug_ls(lapd_sock, "received u-frame\n");

	if (lapd_uframe_function(hdr->control) != LAPD_UFRAME_FUNC_UI &&
	    skb->len != 5) {
		lapd_debug_ls(lapd_sock,
			"received u-frame with wrong size (%d), rejecting\n",
			skb->len);

		lapd_frame_reject(lapd_sock, skb, LAPD_FE_LENGTH);

		return FALSE;
	}

	switch (lapd_uframe_function(hdr->control)) {
	case LAPD_UFRAME_FUNC_SABME:
		queued = lapd_socket_handle_uframe_sabme(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_DM:
		queued = lapd_socket_handle_uframe_dm(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_UI:
		queued = lapd_socket_handle_uframe_ui(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_DISC:
		queued = lapd_socket_handle_uframe_disc(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_UA:
		queued = lapd_socket_handle_uframe_ua(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_FRMR:
		queued = lapd_socket_handle_uframe_frmr(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_XID:
		queued = lapd_socket_handle_uframe_xid(lapd_sock, skb);
	break;

	case LAPD_UFRAME_FUNC_INVALID:
		lapd_msg_ls(lapd_sock, KERN_ERR,
			"received invalid u-frame function %d,"
			" rejecting frame\n",
			hdr->control);

		lapd_frame_reject(lapd_sock, skb, LAPD_FE_UNDEFINED_COMMAND);
		queued = FALSE;
	}

	return queued;
}

int lapd_dlc_recv(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr;

	if (!skb->dev) {

		/* Handle primitives from Layer Management to datalink
		 * procedures
		 */

		struct lapd_mdl_primitive *pri =
			(struct lapd_mdl_primitive *)skb->data;

		switch(pri->type) {
		case LAPD_MDL_ASSIGN_REQUEST:
			lapd_mdl_assign_request(lapd_sock, pri->param);
		break;

		case LAPD_MDL_ERROR_RESPONSE:
			lapd_mdl_error_response(lapd_sock);
		break;

		case LAPD_MDL_ERROR_INDICATION:
			lapd_mdl_error_indication(lapd_sock, pri->param);
		break;

		case LAPD_MDL_REMOVE_REQUEST:
			lapd_mdl_remove_request(lapd_sock);
		break;

		case LAPD_MDL_PERSISTENT_DEACTIVATION:
			lapd_persistent_deactivation(lapd_sock);
		break;

		default:
			BUG();
		}

		return TRUE;
	} else {
		int queued = 0;

		hdr = (struct lapd_data_hdr *)skb->data;

		switch (lapd_frame_type(hdr->control)) {
		case LAPD_FRAME_TYPE_IFRAME:
			queued = lapd_socket_handle_iframe(lapd_sock, skb);
		break;

		case LAPD_FRAME_TYPE_SFRAME:
			queued = lapd_socket_handle_sframe(lapd_sock, skb);
		break;

		case LAPD_FRAME_TYPE_UFRAME:
			queued = lapd_socket_handle_uframe(lapd_sock, skb);
		break;
		}

		return queued;
	}
}

int lapd_dl_establish_request(
	struct lapd_sock *lapd_sock)
{
	switch(lapd_sock->state) {
	case LAPD_DLS_1_TEI_UNASSIGNED:
		lapd_utme_mdl_assign_indication(lapd_sock->usr_tme);
		lapd_change_state(lapd_sock, LAPD_DLS_3_ESTABLISH_AWAITING_TEI);
	break;

	case LAPD_DLS_2_AWAITING_TEI:
		lapd_change_state(lapd_sock, LAPD_DLS_3_ESTABLISH_AWAITING_TEI);
	break;

	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		/* UNEXPECTED???? */
	break;

	case LAPD_DLS_4_TEI_ASSIGNED:
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_sock->layer_3_initiated = TRUE;
		lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_discard_i_queue(lapd_sock);
		lapd_sock->layer_3_initiated = TRUE;
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_i_queue(lapd_sock);
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_sock->layer_3_initiated = TRUE;
		lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_discard_i_queue(lapd_sock);
		lapd_establish_datalink_procedure(lapd_sock);
		lapd_sock->layer_3_initiated = TRUE;
		lapd_change_state(lapd_sock, LAPD_DLS_5_AWAITING_ESTABLISH);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
		return -EINVAL;
	break;
	}

	return 0;
}

int lapd_dl_release_request(
	struct lapd_sock *lapd_sock)
{
	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		lapd_dl_primitive(lapd_sock, LAPD_DL_RELEASE_CONFIRM, 0);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		/* Do nothing */
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_i_queue(lapd_sock);
		lapd_sock->retrans_cnt = 0;
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
		lapd_start_timer(lapd_sock, T200);
		lapd_stop_timer(lapd_sock, T203);
		lapd_change_state(lapd_sock, LAPD_DLS_6_AWAITING_RELEASE);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_discard_i_queue(lapd_sock);
		lapd_sock->retrans_cnt = 0;
		lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
		lapd_start_timer(lapd_sock, T200);
		lapd_change_state(lapd_sock, LAPD_DLS_6_AWAITING_RELEASE);
	break;

	default:
		lapd_unexpected_primitive(lapd_sock);
		return -EINVAL;
	break;
	}

	return 0;
}

void lapd_dl_data_request(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	switch (lapd_sock->state) {
	case LAPD_DLS_4_TEI_ASSIGNED:
		/* Drop frame */
		kfree_skb(skb);
	break;

	case LAPD_DLS_5_AWAITING_ESTABLISH:
		if (lapd_sock->layer_3_initiated)
			lapd_sock_queue_iframe(lapd_sock, skb);
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_sock_queue_iframe(lapd_sock, skb);
		lapd_run_i_queue(lapd_sock);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_sock_queue_iframe(lapd_sock, skb);
	break;

	default:
		kfree_skb(skb);
		lapd_unexpected_primitive(lapd_sock);
	break;
	}
}

void lapd_dl_unit_data_request(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	switch (lapd_sock->state) {
	case LAPD_DLS_1_TEI_UNASSIGNED:
		lapd_sock_queue_uframe(lapd_sock, skb);
		lapd_change_state(lapd_sock, LAPD_DLS_2_AWAITING_TEI);
		lapd_utme_mdl_assign_indication(lapd_sock->usr_tme);
	break;

	case LAPD_DLS_2_AWAITING_TEI:
	case LAPD_DLS_3_ESTABLISH_AWAITING_TEI:
		lapd_sock_queue_uframe(lapd_sock, skb);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_4_TEI_ASSIGNED:
	case LAPD_DLS_5_AWAITING_ESTABLISH:
	case LAPD_DLS_6_AWAITING_RELEASE:
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_8_TIMER_RECOVERY:
		lapd_sock_queue_uframe(lapd_sock, skb);
		lapd_run_ui_queue(lapd_sock);
	}
}

static void lapd_timer_T200(unsigned long data)
{
	struct lapd_sock *lapd_sock = (struct lapd_sock *)data;

	lapd_bh_lock_sock(lapd_sock);

	if (sock_owned_by_user(&lapd_sock->sk)) {
		/* Try again later. */
		sk_reset_timer(&lapd_sock->sk, &lapd_sock->timer_T200,
			jiffies + 20 * HZ / 1000);

		goto socket_owned;
	}

	lapd_debug_dlc(lapd_sock, "T200\n");

	switch (lapd_sock->state) {
	case LAPD_DLS_5_AWAITING_ESTABLISH:
		if (lapd_sock->retrans_cnt == lapd_sock->sap->N200) {
			lapd_discard_i_queue(lapd_sock);
			lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);

			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_G);

			lapd_dl_primitive(lapd_sock,
				LAPD_DL_RELEASE_INDICATION, 0);
		} else {
			lapd_sock->retrans_cnt++;
			lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_SABME,
					1, NULL, 0);
			lapd_start_timer(lapd_sock, T200);
		}
	break;

	case LAPD_DLS_6_AWAITING_RELEASE:
		if (lapd_sock->retrans_cnt == lapd_sock->sap->N200) {
			lapd_change_state(lapd_sock, LAPD_DLS_4_TEI_ASSIGNED);
			lapd_dl_primitive(lapd_sock,
				LAPD_DL_RELEASE_CONFIRM, 0);
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_H);
		} else {
			lapd_sock->retrans_cnt++;
			lapd_send_uframe(lapd_sock, LAPD_UFRAME_FUNC_DISC,
					1, NULL, 0);
			lapd_start_timer(lapd_sock, T200);
		}
	break;

	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		/* TODO: Implement alternative procedure */

		lapd_sock->retrans_cnt = 0;
		lapd_transmit_enquiry_procedure(lapd_sock);
		lapd_sock->retrans_cnt++;
		lapd_change_state(lapd_sock, LAPD_DLS_8_TIMER_RECOVERY);
	break;

	case LAPD_DLS_8_TIMER_RECOVERY:
		if (lapd_sock->retrans_cnt == lapd_sock->sap->N200) {
			lapd_mdl_primitive(lapd_sock, LAPD_MDL_ERROR_INDICATION,
				LAPD_MDL_ERROR_INDICATION_I);

			lapd_establish_datalink_procedure(lapd_sock);
			lapd_sock->layer_3_initiated = FALSE;
			lapd_change_state(lapd_sock,
				LAPD_DLS_5_AWAITING_ESTABLISH);
		} else {
			/* TODO: Implement alternative procedure */
			lapd_transmit_enquiry_procedure(lapd_sock);
			lapd_sock->retrans_cnt++;
		}
	break;

	default:
		lapd_unexpected_timer(lapd_sock);
	break;
	}

socket_owned:
	lapd_bh_unlock_sock(lapd_sock);

	sock_put(&lapd_sock->sk);
}

static void lapd_timer_T203(unsigned long data)
{
	struct lapd_sock *lapd_sock = (struct lapd_sock *)data;

	lapd_debug_dlc(lapd_sock, "T203\n");

	bh_lock_sock(&lapd_sock->sk);

	if (sock_owned_by_user(&lapd_sock->sk)) {
		/* Try again later. */
		sk_reset_timer(&lapd_sock->sk, &lapd_sock->timer_T200,
			jiffies + 20 * HZ / 1000);

		goto socket_owned;
	}

	switch (lapd_sock->state) {
	case LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED:
		lapd_transmit_enquiry_procedure(lapd_sock);
		lapd_sock->retrans_cnt = 0;
		lapd_change_state(lapd_sock, LAPD_DLS_8_TIMER_RECOVERY);
	break;

	default:
		lapd_unexpected_timer(lapd_sock);
	break;
	}

socket_owned:

	bh_unlock_sock(&lapd_sock->sk);

	sock_put(&lapd_sock->sk);
}

void lapd_datalink_state_init(struct lapd_sock *lapd_sock)
{
	init_timer(&lapd_sock->timer_T200);
	lapd_sock->timer_T200.function = lapd_timer_T200;
	lapd_sock->timer_T200.data = (unsigned long)lapd_sock;

	init_timer(&lapd_sock->timer_T203);
	lapd_sock->timer_T203.function = lapd_timer_T203;
	lapd_sock->timer_T203.data = (unsigned long)lapd_sock;

	lapd_sock->state = LAPD_DLS_NULL;

	lapd_sock->peer_receiver_busy = FALSE;
	lapd_sock->own_receiver_busy = FALSE;
	lapd_sock->reject_exception = FALSE;
	lapd_sock->acknowledge_pending = FALSE;
}

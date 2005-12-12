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

#include <linux/kernel.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "datalink.h"

void lapd_deliver_internal_message(
	struct lapd_sock *lapd_sock,
	enum lapd_int_msg_type type,
	int param)
{
	if (type == LAPD_INT_MDL_ASSIGN_REQUEST)
		lapd_mdl_assign_request(lapd_sock, param);
	else if (type == LAPD_INT_MDL_ERROR_RESPONSE)
		lapd_mdl_error_response(lapd_sock);
	else if (type == LAPD_INT_MDL_REMOVE_REQUEST)
		lapd_mdl_remove_request(lapd_sock);
	else
		WARN_ON(1);
}

/*****************+
 * WARNING: this function may be called under lapd_hash_lock and acquires
 * bh_lock_sock. To avoid deadlocks, nothing inside this functions should
 * acquire lapd_hash_lock again
 *
 */

int lapd_backlog_rcv(
	struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	if (!lapd_process_frame(lapd_sock, skb))
		kfree_skb(skb);

	return 0;
}

static int lapd_pass_frame_to_socket(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	int queued;

	/* Ensure serialization within a socket */
	lapd_bh_lock_sock(lapd_sock);

	if (!sock_owned_by_user(&lapd_sock->sk)) {
		queued = lapd_process_frame(lapd_sock, skb);
	} else {
		sk_add_backlog(&lapd_sock->sk, skb);
		queued = 1;
	}

	lapd_bh_unlock_sock(lapd_sock);

	return queued;
}

static inline void lapd_socketless_reply_dm(struct sk_buff *skb)
{
	struct sk_buff *rskb;
	struct lapd_hdr *rhdr;
	struct lapd_hdr *hdr;

	rskb = alloc_skb(sizeof(struct lapd_hdr_e), GFP_ATOMIC);
	if (!rskb)
		return;

	rskb->dev = skb->dev;
	rskb->protocol = __constant_htons(ETH_P_LAPD);
	rskb->h.raw = rskb->nh.raw = rskb->mac.raw = rskb->data;

	rhdr = (struct lapd_hdr *)skb_put(rskb, sizeof(struct lapd_hdr));
	hdr = (struct lapd_hdr *)skb->mac.raw;

	rhdr->addr.sapi = hdr->addr.sapi;
	rhdr->addr.c_r = skb->dev->flags & IFF_ALLMULTI ? 0 : 1;
	rhdr->addr.ea1 = 0;
	rhdr->addr.ea2 = 1;
	rhdr->addr.tei = hdr->addr.tei;
	rhdr->control = lapd_uframe_make_control(
				LAPD_UFRAME_FUNC_DM,
				hdr->u.p_f);

	lapd_send_frame(rskb);
}

/*
 * When we are the network and we cannot associate or create a socket for the
 * incoming frame, we at least reply with a DM. This is expecially useful when
 * the application crashes and the TEs try to re-establsh multiple-frame mode.
 */

static inline void lapd_handle_socketless_frame(struct sk_buff *skb)
{
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

	if (lapd_frame_type(hdr->control) == LAPD_FRAME_TYPE_UFRAME &&
	    (lapd_uframe_function(hdr->control) == LAPD_UFRAME_FUNC_SABME ||
	     lapd_uframe_function(hdr->control) == LAPD_UFRAME_FUNC_DISC)) {
		lapd_socketless_reply_dm(skb);
	}
}

/*************************
 * lapd_pass_frame_to_socket_nt() handles an incoming frame, searches
 * the appropriate socket and creates a new socket if not found.
 *
 * Frames are serialized when relative to the same socket
 */

static inline int lapd_pass_frame_to_socket_nt(
	struct sk_buff *skb)
{
	struct lapd_sock *listening_lapd_sock = NULL;
	struct sock *sk = NULL;
	struct hlist_node *node;
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;
	int queued = 0;

	write_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, lapd_get_hash(skb->dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == skb->dev) {

			if (sk->sk_state == TCP_LISTEN) {
				listening_lapd_sock = lapd_sock;
				continue;
			}


			if (lapd_sock->sapi == hdr->addr.sapi &&
		 	    lapd_sock->tei == hdr->addr.tei) {

				skb->sk = sk;

				write_unlock_bh(&lapd_hash_lock);

				queued = lapd_pass_frame_to_socket(
						lapd_sock, skb);

				goto frame_handled;
			}
		}
	}

	if (listening_lapd_sock) {
		/* A socket has not been found */
		struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;
		struct lapd_sock *new_lapd_sock;

		if (hdr->addr.sapi != LAPD_SAPI_Q931 &&
		    hdr->addr.sapi != LAPD_SAPI_X25) {
			lapd_msg(KERN_WARNING,
				"SAPI %d not supported\n",
				hdr->addr.sapi);
		}

		new_lapd_sock = lapd_new_sock(listening_lapd_sock,
					hdr->addr.tei, hdr->addr.sapi);

		if (!new_lapd_sock) {
			write_unlock_bh(&lapd_hash_lock);
			return FALSE;
		}

		sk_add_node(&new_lapd_sock->sk, lapd_get_hash(skb->dev));
		write_unlock_bh(&lapd_hash_lock);

		skb->sk = &new_lapd_sock->sk;

		{
		struct lapd_new_dlc *new_dlc;
		new_dlc = kmalloc(sizeof(struct lapd_new_dlc), GFP_ATOMIC);
		if (!new_dlc)
			return FALSE;

		new_dlc->lapd_sock = new_lapd_sock;

		hlist_add_head(&new_dlc->node, &listening_lapd_sock->new_dlcs);

		queued = lapd_pass_frame_to_socket(new_lapd_sock, skb);

		if (!sock_flag(&listening_lapd_sock->sk, SOCK_DEAD))
			listening_lapd_sock->sk.sk_data_ready(
				&listening_lapd_sock->sk, skb->len);
		}
	} else {
		lapd_handle_socketless_frame(skb);

		write_unlock_bh(&lapd_hash_lock);
	}

frame_handled:

	return queued;
}

/*************************
 * lapd_pass_frame_to_socket_te() handles an incoming frame, searches
 * the appropriate socket and handles the frame.
 *
 * Frames are serialized when relative to the same socket
 *
 * Locking problem: When receiving a broadcast we have to keep
 * a read-lock on lapd_hash and pass the frame to each single
 * socket handler. Unfortunately, the socket handler may call
 * sk_del_node_init(), so, it needs a write lock on lapd_hash.
 *
 * Solution 1: Easy but ugly... acquire a write lock and keep
 *             it for the whole processing. Low concurrency,
 *             potential deadlocks. The write lock should be
 *             kept when not needed too...
 *
 * Solution 2: Move sk_del_node_init() away and only acquire
 *             needed locks.
 *
 * Solution 3: Use RCU for sockets hash? (Hard as sockets doesn't have
 *             RCU structure)
 */


static inline int lapd_pass_frame_to_socket_te(
	struct sk_buff *skb)
{

	struct sock *sk;
	struct hlist_node *node, *tmp;
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;
	int queued = 0;

	read_lock_bh(&lapd_hash_lock);
	if (hdr->addr.tei == LAPD_BROADCAST_TEI) {
		sk_for_each_safe(sk, node, tmp, lapd_get_hash(skb->dev)) {
			struct lapd_sock *lapd_sock = to_lapd_sock(sk);

			if (lapd_sock->dev == skb->dev &&
			    lapd_sock->sapi == hdr->addr.sapi) {
				struct sk_buff *new_skb;
				new_skb = skb_clone(skb, GFP_ATOMIC);

				new_skb->sk = sk;

				lapd_pass_frame_to_socket(
					to_lapd_sock(sk), new_skb);
			}
		}
	} else {
		sk_for_each(sk, node, lapd_get_hash(skb->dev)) {
			struct lapd_sock *lapd_sock = to_lapd_sock(sk);

			if (lapd_sock->dev == skb->dev &&
			    (lapd_sock->state ==
				LAPD_DLS_4_TEI_ASSIGNED ||
			     lapd_sock->state ==
				LAPD_DLS_5_AWAITING_ESTABLISH ||
			     lapd_sock->state ==
				LAPD_DLS_6_AWAITING_RELEASE ||
			     lapd_sock->state ==
				LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED ||
			     lapd_sock->state ==
				LAPD_DLS_8_TIMER_RECOVERY) &&
			    lapd_sock->sapi == hdr->addr.sapi &&
			    lapd_sock->usr_tme->tei == hdr->addr.tei) {

				skb->sk = sk;

				queued = lapd_pass_frame_to_socket(
						to_lapd_sock(sk), skb);

				break;
			}
		}
	}
	read_unlock_bh(&lapd_hash_lock);

	return queued;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *dev,
	struct packet_type *pt)
#else
int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *dev,
	struct packet_type *pt,
	struct net_device *orig_dev)
#endif
{
	struct lapd_hdr *hdr;
	int queued = 0;

	/* Ignore frames not destined to us */
	if (skb->pkt_type != PACKET_HOST)
		goto not_ours;

	/* Don't mangle buffer if shared */
	if (!(skb = skb_share_check(skb, GFP_ATOMIC)))
		goto err_share_check;

	/* Minimum frame is header + 2 CRC <- not sent by driver */
	if (skb->len < sizeof(struct lapd_hdr)) /* + 2) */
		goto err_small_frame;

	/* Size check and make sure header is contiguous */
	if (!pskb_may_pull(skb, sizeof(struct lapd_hdr)))
		goto err_pskb_may_pull;

	BUG_ON(!skb->dev);

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	hdr = (struct lapd_hdr *)skb->mac.raw;
	if (hdr->addr.ea1 || !hdr->addr.ea2) {
		lapd_msg_dev(skb->dev, KERN_WARNING,
			"improper ea bits in received frame\n");
		goto err_improper_ea;
	}

	if (skb->dev->flags & IFF_ALLMULTI) {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT)
			lapd_ntme_handle_frame(skb);
		else
			queued = lapd_pass_frame_to_socket_nt(skb);
	} else {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT)
			lapd_utme_handle_frame(skb);
		else
			queued = lapd_pass_frame_to_socket_te(skb);

	}

	if (!queued)
		kfree_skb(skb);

	return 0;

err_small_frame:
err_improper_ea:
err_pskb_may_pull:
err_share_check:
	kfree_skb(skb);
not_ours:

	return 0;
}


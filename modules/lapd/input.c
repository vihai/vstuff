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
 */

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/kernel.h>
#include <linux/tcp.h>

#include "lapd.h"
#include "input.h"
#include "output.h"
#include "device.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "datalink.h"
#include "sock_inline.h"

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

	if (!lapd_dlc_recv(lapd_sock, skb))
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
		queued = lapd_dlc_recv(lapd_sock, skb);
	} else {
		sk_add_backlog(&lapd_sock->sk, skb);
		queued = 1;
	}

	lapd_bh_unlock_sock(lapd_sock);

	return queued;
}

static void lapd_socketless_reply_dm(struct sk_buff *skb)
{
	struct sk_buff *rskb;
	struct lapd_data_hdr *rhdr;
	struct lapd_data_hdr *hdr;
	struct lapd_device *dev = to_lapd_dev(skb->dev);

	rskb = lapd_alloc_data_request_skb(dev, sizeof(struct lapd_data_hdr_e));
	if (!rskb)
		return;

	rhdr = (struct lapd_data_hdr *)
		skb_put(rskb, sizeof(struct lapd_data_hdr));
	hdr = (struct lapd_data_hdr *)skb->data;

	rhdr->addr.sapi = hdr->addr.sapi;
	rhdr->addr.c_r = dev->role == LAPD_INTF_ROLE_NT ? 0 : 1;
	rhdr->addr.ea1 = 0;
	rhdr->addr.ea2 = 1;
	rhdr->addr.tei = hdr->addr.tei;
	rhdr->control = lapd_uframe_make_control(
				LAPD_UFRAME_FUNC_DM,
				hdr->u.p_f);

	lapd_ph_data_request(rskb);
}

/*
 * When we are the network and we cannot associate or create a socket for the
 * incoming frame, we at least reply with a DM. This is expecially useful when
 * the application crashes and the TEs try to re-establsh multiple-frame mode.
 */

static void lapd_handle_socketless_frame(struct sk_buff *skb)
{
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;

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

static int lapd_pass_frame_to_socket_nt(
	struct sk_buff *skb)
{
	struct lapd_sock *listening_lapd_sock = NULL;
	struct sock *sk = NULL;
	struct hlist_node *node;
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	int queued = 0;

	write_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, lapd_get_hash(dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == dev) {

			if (sk->sk_state == LAPD_SK_STATE_LISTEN) {
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
		struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;
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

		sk_add_node(&new_lapd_sock->sk, lapd_get_hash(dev));
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
	struct hlist_node *node;
	struct lapd_data_hdr *hdr = (struct lapd_data_hdr *)skb->data;
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	int queued = 0;

	read_lock_bh(&lapd_hash_lock);

	sk_for_each(sk, node, lapd_get_hash(dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == dev &&
		    (sk->sk_state == LAPD_SK_STATE_NORMAL_DLC ||
		    sk->sk_state == LAPD_SK_STATE_BROADCAST_DLC) &&
		    lapd_sock->sapi == hdr->addr.sapi &&
		    lapd_sock->tei == hdr->addr.tei) {

			struct sk_buff *new_skb;

			if (!queued) {
				new_skb = skb;
				queued = TRUE;
			} else {
				new_skb = skb_clone(skb, GFP_ATOMIC);
			}

			new_skb->sk = sk;

			queued = lapd_pass_frame_to_socket(
					to_lapd_sock(sk), new_skb);
		}
	}
	read_unlock_bh(&lapd_hash_lock);

	return queued;
}

static int lapd_mgmt_queue_primitive(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	int skb_len = skb->len;

	skb_set_owner_r(skb, &lapd_sock->sk);

	skb_queue_tail(&lapd_sock->sk.sk_receive_queue, skb);

	if (!sock_flag(&lapd_sock->sk, SOCK_DEAD))
		lapd_sock->sk.sk_data_ready(&lapd_sock->sk, skb_len);

	return TRUE;
}

int lapd_mgmt_backlog_rcv(
	struct sock *sk,
	struct sk_buff *skb)
{
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	if (!lapd_mgmt_queue_primitive(lapd_sock, skb))
		kfree_skb(skb);

	return 0;
}

static int lapd_dispatch_mph_primitive(struct sk_buff *skb)
{
	struct sock *sk;
	struct hlist_node *node;
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	int queued = FALSE;

	read_lock_bh(&lapd_hash_lock);

	sk_for_each(sk, node, lapd_get_hash(dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == dev &&
		    sk->sk_state == LAPD_SK_STATE_MGMT) {

			struct sk_buff *new_skb;

			if (!queued) {
				new_skb = skb;
				queued = TRUE;
			} else {
				new_skb = skb_clone(skb, GFP_ATOMIC);
			}

			new_skb->sk = sk;

			lapd_bh_lock_sock(lapd_sock);

			if (!sock_owned_by_user(&lapd_sock->sk)) {
				queued = lapd_mgmt_queue_primitive(lapd_sock,
									skb);
			} else {
				sk_add_backlog(&lapd_sock->sk, skb);
				queued = TRUE;
			}

			lapd_bh_unlock_sock(lapd_sock);
		}
	}
	read_unlock_bh(&lapd_hash_lock);

	return queued;
}

static int lapd_ph_data_indication(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_data_hdr *hdr;
	int queued;

	/* Minimum frame is header + CRC-16 */
	if (skb->len < sizeof(struct lapd_data_hdr) + sizeof(u16))
		goto err_small_frame;

	hdr = (struct lapd_data_hdr *)skb->data;

	if (hdr->addr.ea1 || !hdr->addr.ea2) {
		lapd_msg_dev(dev, KERN_WARNING,
			"improper ea bits in received frame\n");
		goto err_improper_ea;
	}

	if (dev->role && LAPD_INTF_ROLE_NT) {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT) {
			lapd_ntme_handle_frame(skb);
			queued = FALSE;
		} else {
			queued = lapd_pass_frame_to_socket_nt(skb);
		}
	} else {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT) {
			lapd_utme_handle_frame(skb);
			queued = FALSE;
		} else {
			queued = lapd_pass_frame_to_socket_te(skb);
		}
	}

	return queued;

err_small_frame:
err_improper_ea:

	return FALSE;
}

static int lapd_ph_activate_indication(struct lapd_device *dev)
{
	lapd_debug_dev(dev, "PH-ACTIVATE-INDICATION\n");

	dev->l1_state = LAPD_L1_STATE_AVAILABLE;

	lapd_out_queue_flush(dev);

	return FALSE;
}

static int lapd_ph_deactivate_indication(struct lapd_device *dev)
{
	struct sock *sk;
	struct hlist_node *node;

	lapd_debug_dev(dev, "PH-DEACTIVATE-INDICATION\n");

	dev->l1_state = LAPD_L1_STATE_UNAVAILABLE;

	read_lock_bh(&lapd_hash_lock);

	sk_for_each(sk, node, lapd_get_hash(dev)) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		if (lapd_sock->dev == dev &&
		    sk->sk_state == LAPD_SK_STATE_NORMAL_DLC) {

			lapd_mdl_primitive(
				lapd_sock,
				LAPD_MDL_PERSISTENT_DEACTIVATION, 0);
		}
	}
	read_unlock_bh(&lapd_hash_lock);

	return FALSE;
}

static void lapd_mph_information_indication(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_ctrl_hdr *hdr;

	if (skb->len < sizeof(struct lapd_prim_hdr) +
				sizeof(struct lapd_ctrl_hdr))
		goto err_small_frame;

	hdr = (struct lapd_ctrl_hdr *)
			(skb->data + sizeof(struct lapd_prim_hdr));

	if (hdr->param == LAPD_MPH_II_DISCONNECTED)
		lapd_utme_remove_tei(dev, LAPD_BROADCAST_TEI);

err_small_frame:;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *in_dev,
	struct packet_type *pt)
#else
int lapd_rcv(
	struct sk_buff *skb,
	struct net_device *in_dev,
	struct packet_type *pt,
	struct net_device *orig_dev)
#endif
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	struct lapd_prim_hdr *hdr;
	int queued;

	if (!dev)
		goto not_up;

	/* Ignore frames not destined to us */
	if (skb->pkt_type != PACKET_HOST)
		goto not_ours;

	/* Don't mangle buffer if shared */
	if (!(skb = skb_share_check(skb, GFP_ATOMIC)))
		goto err_share_check;

	if (skb->len < sizeof(struct lapd_prim_hdr))
		goto err_small_frame;

	/* Size check and make sure header is contiguous */
	if (!pskb_may_pull(skb, sizeof(struct lapd_prim_hdr)))
		goto err_pskb_may_pull;

	hdr = (struct lapd_prim_hdr *)skb->data;

	switch(hdr->primitive_type) {
	case LAPD_PH_DATA_INDICATION:
		skb_pull(skb, sizeof(struct lapd_prim_hdr));
		queued = lapd_ph_data_indication(skb);
	break;

	case LAPD_PH_ACTIVATE_INDICATION:
		lapd_ph_activate_indication(dev);
		queued = FALSE;
	break;

	case LAPD_PH_DEACTIVATE_INDICATION:
		lapd_ph_deactivate_indication(dev);
		queued = FALSE;
	break;

	case LAPD_MPH_ERROR_INDICATION:
		queued = lapd_dispatch_mph_primitive(skb);
	break;

	case LAPD_MPH_ACTIVATE_INDICATION:
		queued = lapd_dispatch_mph_primitive(skb);
	break;

	case LAPD_MPH_DEACTIVATE_INDICATION:
		queued = lapd_dispatch_mph_primitive(skb);
	break;

	case LAPD_MPH_INFORMATION_INDICATION:
		lapd_mph_information_indication(skb);
		queued = lapd_dispatch_mph_primitive(skb);
	break;

	case LAPD_PH_DATA_REQUEST:
	case LAPD_PH_ACTIVATE_REQUEST:
	case LAPD_MPH_DEACTIVATE_REQUEST:
	default:
		printk(KERN_WARNING
			"Unexpected primitive received by lapd\n");
		queued = FALSE;
	break;
	}

	if (!queued)
		kfree_skb(skb);

	return 0;

err_pskb_may_pull:
err_small_frame:
err_share_check:
	kfree_skb(skb);
not_ours:
not_up:

	return 0;
}

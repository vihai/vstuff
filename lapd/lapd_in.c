/*

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
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "multiframe.h"

static inline void lapd_handle_socket_uframe_sabme(struct sock *sk,
	struct sk_buff *skb)
{
	printk(KERN_DEBUG "lapd: received u-frame SABME\n");

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;

	if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED ||
	    lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_multiframe_established(sk);

		lapd_send_uframe(sk, UA, hdr->u.p_f, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_ESTABLISH) {
		lapd_send_uframe(sk, UA, hdr->u.p_f, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_RELEASE) {
		lapd_send_uframe(sk, DM, hdr->u.p_f, NULL, 0);
	}
	else {
		printk(KERN_ERR
			"lapd: Unexpected UA in wrong state %d\n",
			lo->status);
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

	lapd_multiframe_released(sk);

//	lapd_start_multiframe_establishment(sk);
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

	if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED) {
		lapd_send_uframe(sk, DM, hdr->u.p_f, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		lapd_multiframe_released(sk);

		lapd_send_uframe(sk, UA, hdr->u.p_f, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_ESTABLISH) {
		lapd_send_uframe(sk, DM, hdr->u.p_f, NULL, 0);
	}
	else if (lo->status == LAPD_DLS_AWAITING_RELEASE) {
		lapd_send_uframe(sk, UA, hdr->u.p_f, NULL, 0);
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

	if (lo->status == LAPD_DLS_AWAITING_ESTABLISH) {
		lapd_multiframe_established(sk);
	}
	else if (lo->status == LAPD_DLS_AWAITING_RELEASE) {
		lapd_multiframe_released(sk);
	}
	else if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED) {
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

int lapd_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt)
{
	// Don't mangle buffer if shared
	if (!(skb = skb_share_check(skb, GFP_ATOMIC)))
		goto err_share_check;

	u8 drop_simul;
	get_random_bytes(&drop_simul, sizeof(drop_simul));

	if (drop_simul > 200) {
		printk(KERN_DEBUG "lapd: "
			"%s: "
			"Simulating frame drop\n",
			skb->dev->name);

		goto err_drop_simul;
	}

	// Minimum frame is header + 2 CRC <- not sent yet by driver
	if (skb->len < sizeof(struct lapd_hdr)) // + 2)
		goto err_small_frame;

	// Size check and make sure header is contiguous
	if (!pskb_may_pull(skb, sizeof(struct lapd_hdr)))
		goto err_pskb_may_pull;

	BUG_ON(!skb->dev);

	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;
	if (hdr->addr.ea1 || !hdr->addr.ea2) {
		printk(KERN_WARNING
			"lapd: int %s: "
			"improper ea bits in received frame\n",
			skb->dev->name);
		goto err_improper_ea;
	}

	if (skb->dev->flags & IFF_ALLMULTI) {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT) {
			lapd_ntme_handle_frame(skb);
			goto frame_handled;
		}

		{
		struct sock *listening_sk = NULL;

		{
		struct sock *sk = NULL;
		struct hlist_node *node;

		read_lock_bh(&lapd_hash_lock);
		sk_for_each(sk, node, &lapd_hash) {
			struct lapd_opt *lo = lapd_sk(sk);

			if (lo->dev == dev) {
				if (sk->sk_state == TCP_LISTEN) {
					listening_sk = sk;
					continue;
				}

				if (lo->sapi == hdr->addr.sapi &&
			 	    lo->tei == hdr->addr.tei) {
					read_unlock_bh(&lapd_hash_lock);

					skb->sk = sk;

					lapd_handle_socket_frame(sk, skb);

					goto frame_handled;
				}
			}
		}
		read_unlock_bh(&lapd_hash_lock);
		}

		// A socket has not been found
		if (listening_sk) {
			struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;
			
			if (hdr->addr.sapi != LAPD_SAPI_Q931 &&
			    hdr->addr.sapi != LAPD_SAPI_X25) {
				printk(KERN_WARNING
					"lapd: SAPI %d not supported\n",
					hdr->addr.sapi);
			}

			struct sock *newsk;
			newsk = lapd_new_sock(listening_sk, hdr->addr.tei,
				hdr->addr.sapi);
			if (!newsk)
				return 0;

			write_lock_bh(&lapd_hash_lock);
			sk_add_node(newsk, &lapd_hash);
			write_unlock_bh(&lapd_hash_lock);

			skb->sk = newsk;

			struct lapd_new_dlc *new_dlc;
			new_dlc = kmalloc(sizeof(struct lapd_new_dlc), GFP_ATOMIC);
			if (!new_dlc) return 0;

			new_dlc->sk = newsk;

			struct lapd_opt *listening_lo = lapd_sk(listening_sk);
			hlist_add_head(&new_dlc->node, &listening_lo->new_dlcs);

			lapd_handle_socket_frame(newsk, skb);

			if (!sock_flag(listening_sk, SOCK_DEAD))
				listening_sk->sk_data_ready(
					listening_sk, skb->len);
		}
		}
	} else {
		if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT) {
			lapd_utme_handle_frame(skb);
			goto frame_handled;
		}

		struct sock *sk;
		struct hlist_node *node;

		read_lock_bh(&lapd_hash_lock);
		sk_for_each(sk, node, &lapd_hash) {
			struct lapd_opt *lo = lapd_sk(sk);

			if (lo->dev == dev &&
			    (hdr->addr.tei == LAPD_BROADCAST_TEI ||
			       lo->usr_tme->tei == hdr->addr.tei)) {

				if (skb->list)
					skb = skb_clone(skb, GFP_ATOMIC);

				skb->sk = sk;

				lapd_handle_socket_frame(sk, skb);
			}
		}
		read_unlock_bh(&lapd_hash_lock);
	}

frame_handled:

	if (!skb->list)
		kfree_skb(skb);

	return 0;

err_small_frame:
err_improper_ea:
err_pskb_may_pull:
err_drop_simul:
err_share_check:

	kfree_skb(skb);

	return 0;
}


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

#include <linux/skbuff.h>

#include "lapd.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

void lapd_out_queue_flush(struct lapd_device *dev)
{
	struct sk_buff *skb;

	spin_lock_bh(&dev->out_queue_lock);

	while ((skb = skb_dequeue(&dev->out_queue)))
		dev_queue_xmit(skb);

	spin_unlock_bh(&dev->out_queue_lock);
}

void lapd_out_queue_drop(struct lapd_device *dev)
{
	spin_lock_bh(&dev->out_queue_lock);
	skb_queue_purge(&dev->out_queue);
	spin_unlock_bh(&dev->out_queue_lock);
}

void lapd_send_frame(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	int err;

	BUG_ON(!skb->dev);

	switch (dev->l1_state) {
	case LAPD_L1_STATE_UNAVAILABLE:
		if (skb->dev->do_ioctl)
			skb->dev->do_ioctl(skb->dev, NULL, LAPD_DEV_IOC_ACTIVATE);

		dev->l1_state = LAPD_L1_STATE_ACTIVATING;

	case LAPD_L1_STATE_ACTIVATING:
		spin_lock_bh(&dev->out_queue_lock);
		skb_queue_tail(&dev->out_queue, skb);
		spin_unlock_bh(&dev->out_queue_lock);
	break;

	case LAPD_L1_STATE_AVAILABLE:
		err = dev_queue_xmit(skb);
		if (err < 0) {

			lapd_msg_dev(dev, KERN_ERR,
				"dev_queue_xmit: %d\n", err);

			kfree_skb(skb);
		}
	break;
	}
}

int lapd_prepare_uframe(struct sock *sk,
	struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f)
{
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	struct lapd_hdr *hdr;
	enum lapd_cr cr;

	BUG_ON(!lapd_sock->dev);

	skb->dev = lapd_sock->dev->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	hdr = (struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = lapd_sock->sapi;

	cr = LAPD_COMMAND;
	switch (function) {
		case LAPD_UFRAME_FUNC_SABME: cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_DM:    cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_UI:    cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_DISC:  cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_UA:    cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_FRMR:  cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_XID:
			lapd_msg_ls(lapd_sock, KERN_ERR,
				"XID unsupported\n");
		break;
		case LAPD_UFRAME_FUNC_INVALID: BUG(); break;
	}

	hdr->addr.c_r = ((cr == LAPD_RESPONSE) ==
			(lapd_sock->dev->role == LAPD_INTF_ROLE_TE)) ? 1 : 0;
	hdr->addr.ea1 = 0;
	hdr->addr.ea2 = 1;

	hdr->addr.tei = lapd_sock->state == LAPD_DLS_LISTENING ?
				LAPD_BROADCAST_TEI :
				lapd_sock->tei;

	hdr->control = lapd_uframe_make_control(function, p_f);

	return 0;
}

int lapd_send_uframe(struct sock *sk,
	enum lapd_uframe_function function,
	int p_f,
	void *data, int datalen)
{
	int err;

	struct sk_buff *skb;
	skb = alloc_skb(sizeof(struct lapd_hdr_e), GFP_ATOMIC);
	if (!skb) {
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	err = lapd_prepare_uframe(sk, skb, function, p_f);
	if (err < 0)
		goto err_prepare_uframe;

	if (data && datalen)
		memcpy(skb_put(skb, datalen), data, datalen);

	lapd_send_frame(skb);

	return 0;

err_prepare_uframe:
	kfree_skb(skb);
err_alloc_skb:

	return err;
}

void lapd_queue_completed_uframe(
	struct sock *sk,
	struct sk_buff *skb)
{
	skb->sk = sk;
	skb_queue_tail(&to_lapd_sock(sk)->u_queue, skb);
}

void lapd_send_completed_uframe(struct sk_buff *skb)
{
	lapd_send_frame(skb);
}

void lapd_out_init(void)
{
}

void lapd_out_exit(void)
{
}

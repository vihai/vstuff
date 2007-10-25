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

	while ((skb = skb_dequeue(&dev->out_queue))) {
		struct lapd_prim_hdr *prim_hdr;

		skb_push(skb, sizeof(struct lapd_prim_hdr));
		prim_hdr = (struct lapd_prim_hdr *)skb->data;
		prim_hdr->primitive_type = LAPD_PH_DATA_REQUEST;

		memset(skb_put(skb, sizeof(u16)), 0, sizeof(u16));

		dev_queue_xmit(skb);
	}

	spin_unlock_bh(&dev->out_queue_lock);
}

void lapd_out_queue_drop(struct lapd_device *dev)
{
	spin_lock_bh(&dev->out_queue_lock);
	skb_queue_purge(&dev->out_queue);
	spin_unlock_bh(&dev->out_queue_lock);
}

void lapd_send_ph_primitive(
	struct lapd_device *dev,
	enum lapd_primitive_type primitive_type,
	int param)
{
	struct sk_buff *skb;
	struct lapd_prim_hdr *prim_hdr;
	struct lapd_ctrl_hdr *ctrl_hdr;

	skb = alloc_skb(sizeof(struct lapd_prim_hdr) +
			sizeof(struct lapd_ctrl_hdr), GFP_ATOMIC);
	if (!skb)
		goto err_alloc_skb;

	skb->dev = dev->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	prim_hdr = (struct lapd_prim_hdr *)
		skb_put(skb, sizeof(struct lapd_prim_hdr));
	ctrl_hdr = (struct lapd_ctrl_hdr *)
		skb_put(skb, sizeof(struct lapd_ctrl_hdr));

	prim_hdr->primitive_type = primitive_type;
	ctrl_hdr->param = param;

	dev_queue_xmit(skb);

	return;

err_alloc_skb:;
}

void lapd_ph_data_request(struct sk_buff *skb)
{
	struct lapd_device *dev = to_lapd_dev(skb->dev);
	int err;

	BUG_ON(!skb->dev);

	switch (dev->l1_state) {
	case LAPD_L1_STATE_UNAVAILABLE:
		dev->l1_state = LAPD_L1_STATE_ACTIVATING;
		lapd_send_ph_primitive(dev, LAPD_PH_ACTIVATE_REQUEST, 0);
		spin_lock_bh(&dev->out_queue_lock);
		skb_queue_tail(&dev->out_queue, skb);
		spin_unlock_bh(&dev->out_queue_lock);
	break;

	case LAPD_L1_STATE_ACTIVATING:
		spin_lock_bh(&dev->out_queue_lock);
		skb_queue_tail(&dev->out_queue, skb);
		spin_unlock_bh(&dev->out_queue_lock);
	break;

	case LAPD_L1_STATE_AVAILABLE: {
		struct lapd_prim_hdr *prim_hdr;

		skb_push(skb, sizeof(struct lapd_prim_hdr));
		prim_hdr = (struct lapd_prim_hdr *)skb->data;
		prim_hdr->primitive_type = LAPD_PH_DATA_REQUEST;

		memset(skb_put(skb, sizeof(u16)), 0, sizeof(u16));

		err = dev_queue_xmit(skb);
		if (err < 0) {

			lapd_msg_dev(dev, KERN_ERR,
				"dev_queue_xmit: %d\n", err);

			kfree_skb(skb);
		}
	}
	break;
	}
}

int lapd_prepare_uframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f)
{
	struct lapd_data_hdr *hdr;
	enum lapd_cr cr;

	BUG_ON(!lapd_sock->dev);

	hdr = (struct lapd_data_hdr *)
		skb_put(skb, sizeof(struct lapd_data_hdr));

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

struct sk_buff *lapd_alloc_data_request_skb(
	struct lapd_device *dev,
	unsigned int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(struct lapd_prim_hdr) + size, GFP_ATOMIC);
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct lapd_prim_hdr));

	skb->dev = dev->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	return skb;
}

int lapd_send_uframe(
	struct lapd_sock *lapd_sock,
	enum lapd_uframe_function function,
	int p_f,
	void *data, int datalen)
{
	struct sk_buff *skb;
	int err;

	BUG_ON(!lapd_sock->dev->dev);

	skb = lapd_alloc_data_request_skb(lapd_sock->dev,
				sizeof(struct lapd_data_hdr_e));
	if (!skb) {
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	err = lapd_prepare_uframe(lapd_sock, skb, function, p_f);
	if (err < 0)
		goto err_prepare_uframe;

	if (data && datalen)
		memcpy(skb_put(skb, datalen), data, datalen);

	lapd_ph_data_request(skb);

	return 0;

err_prepare_uframe:
	kfree_skb(skb);
err_alloc_skb:

	return err;
}

void lapd_sock_queue_uframe(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	skb->dev = lapd_sock->dev->dev;
	skb->sk = &lapd_sock->sk;

	skb_queue_tail(&lapd_sock->u_queue, skb);
}

void lapd_out_init(void)
{
}

void lapd_out_exit(void)
{
}

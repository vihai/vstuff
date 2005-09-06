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

#include <linux/skbuff.h>

#include "lapd.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

inline int lapd_send_frame(struct sk_buff *skb)
{
	int err;

	BUG_ON(!skb->dev);

	if((err = dev_queue_xmit(skb)) < 0) {

		lapd_printk_dev(KERN_ERR, skb->dev,
			"dev_queue_xmit: %d\n", err);

		kfree_skb(skb);

		return err;
	}

	return skb->len;
}

int lapd_prepare_uframe(struct sock *sk,
	struct sk_buff *skb,
	enum lapd_uframe_function function,
	int p_f)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_ON(!lo->dev);

	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	struct lapd_hdr *hdr =
		(struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = lo->sapi;

	enum lapd_cr cr = LAPD_COMMAND;
	switch (function) {
		case LAPD_UFRAME_FUNC_SABME: cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_DM:    cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_UI:    cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_DISC:  cr = LAPD_COMMAND; break;
		case LAPD_UFRAME_FUNC_UA:    cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_FRMR:  cr = LAPD_RESPONSE; break;
		case LAPD_UFRAME_FUNC_XID:
			lapd_printk_sk(KERN_ERR, sk,
				"XID unsupported\n");
		break;
		case LAPD_UFRAME_FUNC_INVALID: BUG(); break;
	}

	hdr->addr.c_r = ((cr == LAPD_RESPONSE) == !lo->nt_mode)?1:0;
	hdr->addr.ea1 = 0;
	hdr->addr.ea2 = 1;

	hdr->addr.tei = lo->state == LAPD_DLS_LISTENING ?
				LAPD_BROADCAST_TEI :
				lo->tei;

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

	return lapd_send_frame(skb);

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
	skb_queue_tail(&lapd_sk(sk)->u_queue, skb);
}

void lapd_flush_uqueue(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&lapd_sk(sk)->u_queue))) {
		lapd_send_frame(skb);
	}
}

int lapd_send_completed_uframe(struct sk_buff *skb)
{
	return lapd_send_frame(skb);
}


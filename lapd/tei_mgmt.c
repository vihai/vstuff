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
#include "lapd_out.h"
#include "tei_mgmt.h"

inline int lapd_tm_send(
	struct net_device *dev,
	u8 message_type, u16 ri, u8 ai)
{
	BUG_ON(!dev);

	struct sk_buff *skb;
	skb = alloc_skb(sizeof(struct lapd_hdr) +
		sizeof(struct lapd_tei_mgmt_body),
		GFP_ATOMIC);

	skb->dev = dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	struct lapd_hdr *hdr =
		(struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = LAPD_SAPI_TEI_MGMT;

	hdr->addr.c_r = (dev->flags & IFF_ALLMULTI)?1:0;
	hdr->addr.ea1 = 0;
	hdr->addr.ea2 = 1;
	hdr->addr.tei = LAPD_BROADCAST_TEI;

	hdr->control = lapd_uframe_make_control(LAPD_UFRAME_FUNC_UI, 0/* p_f*/);

	struct lapd_tei_mgmt_body *tm =
		 (struct lapd_tei_mgmt_body *)skb_put(skb,
			 sizeof(struct lapd_tei_mgmt_body));

	tm->entity = LAPD_TEI_ENTITY;
	tm->message_type = message_type;
	tm->ri = ri;
	tm->ai = ai;
	tm->ai_ext = 1;

	return lapd_send_frame(skb);
}

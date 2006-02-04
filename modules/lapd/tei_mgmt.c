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

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/skbuff.h>

#include "lapd.h"
#include "lapd_out.h"
#include "tei_mgmt.h"

int lapd_tm_send_multiai(
	struct lapd_device *dev,
	u8 message_type, u16 ri, u8 ais[], int nai)
{
	struct sk_buff *skb;
	struct lapd_hdr *hdr;
	struct lapd_tei_mgmt_hdr *tm;
	int i;

	skb = alloc_skb(sizeof(struct lapd_hdr) +
		sizeof(struct lapd_tei_mgmt_hdr) +
		nai,
		GFP_ATOMIC);

	skb->dev = dev->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);
	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

	hdr = (struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = LAPD_SAPI_TEI_MGMT;

	hdr->addr.c_r = lapd_make_cr(dev, LAPD_COMMAND);
	hdr->addr.ea1 = 0;
	hdr->addr.ea2 = 1;
	hdr->addr.tei = LAPD_BROADCAST_TEI;

	hdr->control = lapd_uframe_make_control(LAPD_UFRAME_FUNC_UI, 0/* p_f*/);

	tm = (struct lapd_tei_mgmt_hdr *)skb_put(skb, sizeof(*tm));
	tm->entity = LAPD_TEI_ENTITY;
	tm->message_type = message_type;
	tm->ri = ri;

	for (i=0; i<nai; i++) {
		struct lapd_tei_mgmt_ai *tm_ai =
			(struct lapd_tei_mgmt_ai *)
			skb_put(skb, sizeof(*tm_ai));
		
		tm_ai->value = ais[i];
		tm_ai->ext = (i < nai - 1) ? 0 : 1;
	}

	return lapd_send_frame(skb);
}

int lapd_tm_send(
	struct lapd_device *dev,
	u8 message_type, u16 ri, u8 ai)
{
	return lapd_tm_send_multiai(dev, message_type, ri, &ai, 1);
}

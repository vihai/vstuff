/*
 * Socket Buffer Userland Implementation
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdlib.h>

#include "libskb.h"

struct sk_buff *alloc_skb(int size, int gfp)
{
	struct sk_buff *skb;

	skb = malloc(sizeof(*skb) + size);
	if (!skb)
		return NULL;

	skb->size = size;
	skb->len = 0;

	skb->data = skb + sizeof(*skb);
	skb->tail = skb->data;
	skb->end = skb->data + size;

	return skb;
}

void kfree_skb(struct sk_buff *skb)
{
	free(skb);
}

void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len) {
		skb->len  = len;
		skb->tail = skb->data + len;
	}
}

int skb_tailroom(const struct sk_buff *skb)
{
	return skb->end - skb->tail;
}

void *skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb->tail;

	if (skb->len + len > skb->size)
		return NULL;

	skb->tail += len;
	skb->len  += len;

	return tmp;
}

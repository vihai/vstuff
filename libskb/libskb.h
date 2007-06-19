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

#ifndef _LIBSKB_H
#define _LIBSKB_H

#include <list.h>

#ifndef GFP_KERNEL
#define GFP_KERNEL 0
#endif

#ifndef GFP_ATOMIC
#define GFP_ATOMIC 1
#endif

struct sk_buff
{
	struct list_head node;

	int size;

	void *data;
	void *tail;
	void *end;

	int len;
};

struct sk_buff *alloc_skb(int size, int gfp);
void kfree_skb(struct sk_buff *skb);
void skb_trim(struct sk_buff *skb, unsigned int len);
int skb_tailroom(const struct sk_buff *skb);
void *skb_put(struct sk_buff *skb, unsigned int len);

#endif


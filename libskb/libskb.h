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

struct sk_buff
{
	int size;

	void *data;
	void *tail;
	void *end;

	int len;
};

struct sk_buff *skb_alloc(int size, int gfp);
void kfree_skb(struct sk_buff *skb);
void skb_trim(struct sk_buff *skb, unsigned int len);
int skb_tailroom(const struct sk_buff *skb);
void *skb_put(struct sk_buff *skb, unsigned int len);

#endif


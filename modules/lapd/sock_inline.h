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

#ifndef _LAPD_SOCK_INLINE_H
#define _LAPD_SOCK_INLINE_H

#include "lapd.h"
#include "device.h"

static inline struct hlist_head *lapd_get_hash(struct lapd_device *dev)
{
	return &lapd_hash[dev->dev->ifindex & (LAPD_HASHSIZE - 1)];
}

static inline void lapd_bh_lock_sock(struct lapd_sock *lapd_sock)
{
	bh_lock_sock(&lapd_sock->sk);
}

static inline void lapd_bh_unlock_sock(struct lapd_sock *lapd_sock)
{
	bh_unlock_sock(&lapd_sock->sk);
}

static inline void lapd_lock_sock(struct lapd_sock *lapd_sock)
{
	lock_sock(&lapd_sock->sk);
}

static inline void lapd_release_sock(struct lapd_sock *lapd_sock)
{
	release_sock(&lapd_sock->sk);
}

#endif

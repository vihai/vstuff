/*
 * KStreamer kernel infrastructure core
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_STREAMFRAME_H
#define _KS_STREAMFRAME_H

#ifdef __KERNEL__

#include <asm/atomic.h>

struct ks_streamframe
{
	atomic_t refcnt;
	u16 size;
	u16 len;

	u8 data[0];
};

struct ks_streamframe *ks_sf_alloc(void);

static inline struct ks_streamframe *ks_sf_get(
		struct ks_streamframe *sf)
{
	atomic_inc(&sf->refcnt);

	return sf;
}

static inline void ks_sf_put(struct ks_streamframe *sf)
{
	if (atomic_dec_and_test(&sf->refcnt))
		kfree(sf);
}

#endif
#endif

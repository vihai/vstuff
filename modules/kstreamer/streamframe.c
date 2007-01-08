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

#include <linux/kernel.h>
#include <linux/module.h>

#include "streamframe.h"

struct ks_streamframe *ks_sf_alloc(void)
{
	struct ks_streamframe *sf;

	sf = kmalloc(512, GFP_ATOMIC);
	if (!sf)
		return NULL;

	atomic_set(&sf->refcnt, 1);
	sf->size = 512 - sizeof(*sf);
	sf->len = 0;

	return sf;
}
EXPORT_SYMBOL(ks_sf_alloc);

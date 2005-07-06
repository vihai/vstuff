/*
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 */

#ifndef _SOFTPORT_H
#define _SOFTPORT_H

#ifdef __KERNEL__

#include <visdn.h>

#define sb_DRIVER_NAME "visdn-softport"
#define sb_DRIVER_PREFIX sb_DRIVER_NAME ": "
#define sb_DRIVER_DESCR "Softport implementation"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_sb_chan(visdn_chan) container_of(visdn_chan, struct sb_chan, visdn_chan)

struct sb_chan
{
	int index;

	struct hlist_node index_hlist_node;

	struct visdn_chan visdn_chan;
};

#endif

#endif

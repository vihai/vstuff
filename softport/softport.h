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

#define sb_DRIVER_NAME "visdn-softport"
#define sb_DRIVER_PREFIX sb_DRIVER_NAME ": "
#define sb_DRIVER_DESCR "Softport implementation"

#define SB_CHAN_HASHBITS 8

#include <visdn.h>

struct sb_chan
{
	struct hlist_node index_hlist;
	struct visdn_chan visdn_chan;
	struct class_device class_dev;
	int index;
};

#endif

#endif

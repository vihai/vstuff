/*
 * vISDN gateway between vISDN's crossconnector and Linux's ppp subsystem
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_PPP_H
#define _VISDN_PPP_H

#ifdef __KERNEL__

#include <visdn.h>
#include <linux/skbuff.h>
#include <linux/ppp_channel.h>

#define vppp_MODULE_NAME "visdn-ppp"
#define vppp_MODULE_PREFIX vppp_MODULE_NAME ": "
#define vppp_MODULE_DESCR "vISDN ppp gateway module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vppp_chan(visdn_chan) container_of(visdn_chan, struct vppp_chan, visdn_chan)

struct vppp_chan
{
	int index;

	struct hlist_node index_hlist_node;

	struct visdn_chan visdn_chan;

	struct ppp_channel ppp_chan;
};

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif

#endif
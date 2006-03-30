/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_ROUTER_H
#define _VISDN_ROUTER_H

#include <kernel_config.h>

/* See core.h for IOC allocation */
#define VISDN_IOC_CONNECT		_IOR(0xd0, 0x02, unsigned int)
#define VISDN_IOC_DISCONNECT		_IOR(0xd0, 0x03, unsigned int)
#define VISDN_IOC_DISCONNECT_ENDPOINT	_IOR(0xd0, 0x04, unsigned int)
#define VISDN_IOC_ENABLE_PATH		_IOR(0xd0, 0x05, unsigned int)
#define VISDN_IOC_DISABLE_PATH		_IOR(0xd0, 0x06, unsigned int)

struct visdn_connect
{
	int path_id;

	int src_chan_id;
	int dst_chan_id;

	int flags;
};

#define VISDN_CONNECT_FLAG_PERMANENT	(1 << 0)

#ifdef __KERNEL__

struct visdn_router_node;

struct visdn_router_arch
{
	struct list_head node;

	int cost;

	struct visdn_router_node *src_node;
	struct visdn_router_node *dst_node;

	/* Leg which corresponds to arch's source */
	struct visdn_leg *src_leg;
};

struct visdn_router_node
{
	struct list_head node;

	int is_channel;

	int cost;
	int done;

	struct visdn_router_node *prev;
	struct visdn_router_arch *prev_thru;
};

extern void visdn_router_add_node(struct visdn_router_node *node);
extern void visdn_router_del_node(struct visdn_router_node *node);
extern void visdn_router_add_arch(struct visdn_router_arch *arch);
extern void visdn_router_del_arch(struct visdn_router_arch *arch);

void visdn_router_run(struct visdn_router_node *start);
void visdn_router_print_node_name(struct visdn_router_node *node);

void visdn_router_lock(void);
void visdn_router_unlock(void);

int visdn_router_modinit(void);
void visdn_router_modexit(void);

#endif

#endif

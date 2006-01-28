/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
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
#define VISDN_IOC_CONNECT_PATH		_IOR(0xd0, 0x04, unsigned int)
#define VISDN_IOC_DISCONNECT_PATH	_IOR(0xd0, 0x05, unsigned int)
#define VISDN_IOC_ENABLE_PATH		_IOR(0xd0, 0x06, unsigned int)
#define VISDN_IOC_DISABLE_PATH		_IOR(0xd0, 0x07, unsigned int)

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

struct visdn_chan;

extern int visdn_connect_path(
	struct visdn_chan *src_chan,
	struct visdn_chan *dst_chan,
	struct file *file,
	unsigned long flags);

extern int visdn_connect_path_with_id(
	int chan1_id,
	int chan2_id,
	struct file *file,
	unsigned long flags);

extern int visdn_disconnect_path(
	struct visdn_chan *src_chan);
extern int visdn_disconnect_path_with_id(
	int chan1_id);

extern int visdn_enable_path(
	struct visdn_chan *src_chan);
extern int visdn_enable_path_with_id(
	int chan1_id);

extern int visdn_disable_path(
	struct visdn_chan *src_chan);
extern int visdn_disable_path_with_id(
	int chan1_id);
extern int visdn_find_lowest_mtu(struct visdn_leg *leg);

#endif

#endif

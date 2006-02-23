/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_SYS_CHAN_H
#define _HFC_SYS_CHAN_H

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>

#include "util.h"
#include "fifo.h"

#define to_sys_chan(chan)	\
		container_of(chan, struct hfc_sys_chan, visdn_chan)

struct hfc_sys_chan;
struct hfc_fifo;

extern struct visdn_chan_class hfc_sys_chan_class;

struct hfc_sys_port;
struct hfc_sys_chan {
	struct hfc_sys_port *port;

	int id;

	struct hfc_fifo rx_fifo;
	struct hfc_fifo tx_fifo;

	struct visdn_chan visdn_chan;

	struct hfc_e1_chan *connected_e1_chan;
	struct hfc_pcm_chan *connected_pcm_chan;

	struct work_struct rx_work;
};

extern void hfc_sys_chan_init(
	struct hfc_sys_chan *chan,
	struct hfc_sys_port *port,
	const char *name,
	int id);
extern int hfc_sys_chan_register(
	struct hfc_sys_chan *chan);
extern void hfc_sys_chan_unregister(
	struct hfc_sys_chan *chan);

#endif

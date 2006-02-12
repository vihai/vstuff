/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_CHAN_H
#define _HFC_CHAN_H

#include <linux/visdn/chan.h>

#include "util.h"
#include "fifo.h"

struct hfc_st_chan;
struct hfc_fifo;

struct hfc_st_port;
struct hfc_st_chan {
	struct hfc_st_port *port;

	int id;

	struct hfc_fifo *rx_fifo;
	struct hfc_fifo *tx_fifo;

	struct visdn_chan visdn_chan;

	struct work_struct rx_work;
};

extern void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id);

extern int hfc_st_chan_register(struct hfc_st_chan *chan);
extern void hfc_st_chan_unregister(struct hfc_st_chan *chan);

#endif

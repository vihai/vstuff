/*
 * Cologne Chip's HFC-USB vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_ST_CHAN_H
#define _HFC_ST_CHAN_H

#include <linux/visdn/chan.h>

#include "fifo.h"

struct hfc_st_chan {
	struct visdn_chan visdn_chan;

	struct hfc_st_port *port;

	int id;

	int subchannel_bit_count;

	struct hfc_fifo *rx_fifo;
	struct hfc_fifo *tx_fifo;

	struct hfc_led *led;
};

int hfc_st_chan_register(struct hfc_st_chan *chan);
void hfc_st_chan_unregister(struct hfc_st_chan *chan);

void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	struct hfc_led *led,
	int subchannel_bit_count,
	struct hfc_fifo *rx_fifo,
	struct hfc_fifo *tx_fifo);

#endif

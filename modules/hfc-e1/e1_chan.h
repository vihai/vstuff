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

#ifndef _HFC_E1_CHAN_H
#define _HFC_E1_CHAN_H

#include <linux/visdn/chan.h>

#include "util.h"

#define to_e1_chan(chan)	\
		container_of(chan, struct hfc_e1_chan, visdn_chan)

enum hfc_e1_chan_status {
	HFC_ST_CHAN_STATUS_FREE,
	HFC_ST_CHAN_STATUS_OPEN,
	HFC_ST_CHAN_STATUS_OPEN_BERT,
};

extern struct visdn_chan_class hfc_e1_chan_class;

struct hfc_e1_port;
struct hfc_e1_chan {
	struct hfc_e1_port *port;

	enum hfc_e1_chan_status status;

	int id;

	int hw_index;

	struct visdn_chan visdn_chan;

	struct hfc_sys_chan *connected_sys_chan;
	struct hfc_pcm_chan *connected_pcm_chan;
};

void hfc_e1_chan_init(
	struct hfc_e1_chan *chan,
	struct hfc_e1_port *port,
	int id,
	int hw_index);

int hfc_e1_chan_register(struct hfc_e1_chan *chan);
void hfc_e1_chan_unregister(struct hfc_e1_chan *chan);

#endif

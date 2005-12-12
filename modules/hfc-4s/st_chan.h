/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
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

#include "util.h"

#define to_st_chan(chan)	\
		container_of(chan, struct hfc_st_chan, visdn_chan)

enum hfc_st_chan_status {
	HFC_ST_CHAN_STATUS_FREE,
	HFC_ST_CHAN_STATUS_OPEN,
	HFC_ST_CHAN_STATUS_OPEN_BERT,
};

extern struct visdn_chan_class hfc_st_chan_class;

struct hfc_st_port;
struct hfc_st_chan {
	struct hfc_st_port *port;

	enum hfc_st_chan_status status;

	int id;

	int hw_index;

	int subchannel_bit_count;
	int subchannel_bit_start;
	int native_bitrate;

	struct visdn_chan visdn_chan;

	struct hfc_sys_chan *connected_sys_chan;
	struct hfc_pcm_chan *connected_pcm_chan;
};

extern void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	int subchannel_bit_count,
	int subchannel_bit_start,
	int native_bitrate);

int hfc_st_chan_register(struct hfc_st_chan *chan);
void hfc_st_chan_unregister(struct hfc_st_chan *chan);

#endif

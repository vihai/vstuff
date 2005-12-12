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

#ifndef _HFC_PCM_CHAN_H
#define _HFC_PCM_CHAN_H

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>

#include "util.h"

#define to_pcm_chan(chan)	\
		container_of(chan, struct hfc_pcm_chan, visdn_chan)

extern struct visdn_chan_class hfc_pcm_chan_class;

enum hfc_pcm_chan_status {
	HFC_PCM_CHAN_STATUS_FREE,
	HFC_PCM_CHAN_STATUS_OPEN,
};

struct hfc_pcm_chan;
struct hfc_pcm_slot
{
	struct hfc_pcm_chan *chan;

	int hw_index;
	enum hfc_direction direction;

	struct hfc_chan_simplex *connected_chan;

	int used;
};

struct hfc_pcm_port;
struct hfc_pcm_chan {
	struct hfc_pcm_port *port;

	enum hfc_pcm_chan_status status;

	int id;

	int hw_index;

	struct hfc_pcm_slot rx_slot;
	struct hfc_pcm_slot tx_slot;

	struct visdn_chan visdn_chan;

	struct hfc_sys_chan *connected_sys_chan;
	struct hfc_st_chan *connected_st_chan;
};

void hfc_pcm_chan_init(
	struct hfc_pcm_chan *chan,
	struct hfc_pcm_port *port,
	const char *name,
	int id,
	int hw_index);
int hfc_pcm_chan_register(
	struct hfc_pcm_chan *chan);
void hfc_pcm_chan_unregister(
	struct hfc_pcm_chan *chan);

#endif

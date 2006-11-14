/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_PCM_CHAN_H
#define _HFC_PCM_CHAN_H

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>

#include "util.h"

#define to_pcm_chan(chan)	\
		container_of(chan, struct hfc_pcm_chan, visdn_chan)

extern struct ks_chan_ops hfc_pcm_chan_rx_chan_ops;
extern struct ks_chan_ops hfc_pcm_chan_tx_chan_ops;

enum hfc_pcm_chan_status {
	HFC_PCM_CHAN_STATUS_FREE,
	HFC_PCM_CHAN_STATUS_OPEN,
};

struct hfc_pcm_chan;
struct hfc_pcm_chan_rx
{
	struct ks_chan ks_chan;

	struct hfc_pcm_chan *chan;

	enum hfc_pcm_chan_status status;
};

struct hfc_pcm_chan_tx
{
	struct ks_chan ks_chan;

	struct hfc_pcm_chan *chan;

	enum hfc_pcm_chan_status status;
};

struct hfc_pcm_port;
struct hfc_pcm_chan {
	struct ks_node ks_node;

	struct hfc_pcm_port *port;

	int timeslot;

	struct hfc_pcm_chan_rx rx;
	struct hfc_pcm_chan_tx tx;
};

struct hfc_pcm_chan *hfc_pcm_chan_alloc(int flags);
void hfc_pcm_chan_init(
	struct hfc_pcm_chan *chan,
	struct hfc_pcm_port *port,
	const char *name,
	int timeslot);
int hfc_pcm_chan_register(struct hfc_pcm_chan *chan);
void hfc_pcm_chan_unregister(struct hfc_pcm_chan *chan);

static inline struct hfc_pcm_chan *hfc_pcm_chan_get(struct hfc_pcm_chan *chan)
{
	ks_node_get(&chan->ks_node);

	return chan;
}

static inline void hfc_pcm_chan_put(struct hfc_pcm_chan *chan)
{
	ks_node_put(&chan->ks_node);
}

#endif

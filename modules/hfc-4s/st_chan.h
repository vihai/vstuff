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

#ifndef _HFC_ST_CHAN_H
#define _HFC_ST_CHAN_H

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/duplex.h>

#include "util.h"

/*#define to_st_chan(chan)	\
		container_of(chan, struct hfc_st_chan, visdn_chan)*/
#define to_st_chan_rx(chan)	\
		container_of(chan, struct hfc_st_chan_rx, ks_chan)
#define to_st_chan_tx(chan)	\
		container_of(chan, struct hfc_st_chan_tx, ks_chan)

enum hfc_st_chan_status {
	HFC_ST_CHAN_STATUS_FREE,
	HFC_ST_CHAN_STATUS_OPEN,
	HFC_ST_CHAN_STATUS_OPEN_BERT,
};

// extern struct visdn_chan_class hfc_st_chan_class;

extern struct ks_chan_ops hfc_st_chan_rx_chan_ops;
extern struct ks_chan_ops hfc_st_chan_tx_chan_ops;

struct hfc_st_chan;

struct hfc_st_chan_rx
{
	struct ks_chan ks_chan;

	struct hfc_st_chan *chan;

	enum hfc_st_chan_status status;
};

struct hfc_st_chan_tx
{
	struct ks_chan ks_chan;

	struct hfc_st_chan *chan;

	enum hfc_st_chan_status status;
};

struct hfc_st_port;
struct hfc_st_chan {
	struct hfc_st_port *port;

	int id;
	int hw_index;

	struct ks_node ks_node;
	struct ks_duplex ks_duplex;

	struct hfc_st_chan_rx rx;
	struct hfc_st_chan_tx tx;

	int subchannel_bit_count;
	int subchannel_bit_start;
//	int native_bitrate;
};

struct hfc_st_chan *hfc_st_chan_create(
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
void hfc_st_chan_destroy(struct hfc_st_chan *chan);

static inline struct hfc_st_chan *hfc_st_chan_get(struct hfc_st_chan *chan)
{
	ks_node_get(&chan->ks_node);

	return chan;
}

static inline void hfc_st_chan_put(struct hfc_st_chan *chan)
{
	ks_node_put(&chan->ks_node);
}

static inline struct hfc_st_chan_rx *hfc_st_chan_rx_get(
	struct hfc_st_chan_rx *chan)
{
	ks_chan_get(&chan->ks_chan);

	return chan;
}

static inline void hfc_st_chan_rx_put(struct hfc_st_chan_rx *chan)
{
	ks_chan_put(&chan->ks_chan);
}

static inline struct hfc_st_chan_tx *hfc_st_chan_tx_get(
	struct hfc_st_chan_tx *chan)
{
	ks_chan_get(&chan->ks_chan);

	return chan;
}

static inline void hfc_st_chan_tx_put(struct hfc_st_chan_tx *chan)
{
	ks_chan_put(&chan->ks_chan);
}

#endif

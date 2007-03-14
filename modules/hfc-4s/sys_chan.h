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

#ifndef _HFC_SYS_CHAN_H
#define _HFC_SYS_CHAN_H

#include <linux/interrupt.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/dynattr.h>
#include <linux/kstreamer/duplex.h>

#include <linux/kstreamer/hdlc_framer.h>
#include <linux/kstreamer/octet_reverser.h>

#include "util.h"
#include "fifo.h"

#define to_sys_chan_rx(chan)	\
		container_of(chan, struct hfc_sys_chan_rx, ks_chan)

#define to_sys_chan_tx(chan)	\
		container_of(chan, struct hfc_sys_chan_tx, ks_chan)

/*#define to_sys_chan(chan)	\
		container_of(chan, struct hfc_sys_chan, visdn_chan)*/

struct hfc_sys_chan;
struct hfc_fifo;

//extern struct visdn_chan_class hfc_sys_chan_class;

//extern struct ks_chan_ops hfc_sys_chan_tx_chan_ops;
//extern struct ks_chan_ops hfc_sys_chan_rx_chan_ops;

struct hfc_sys_chan;

struct hfc_hdlc_framer
{
	struct ks_dynattr_instance dynattr;

	struct ks_hdlc_framer_descr descr;
};

struct hfc_octet_reverser
{
	struct ks_dynattr_instance dynattr;

	struct ks_octet_reverser_descr descr;
};

struct hfc_sys_chan_rx
{
	struct ks_chan ks_chan;

	struct hfc_sys_chan *chan;

	struct hfc_hdlc_framer hdlc_framer;
	struct hfc_octet_reverser octet_reverser;

	struct hfc_fifo fifo;
	int fifo_enabled;

	struct tasklet_struct tasklet;
};

#define HFC_SYS_CHAN_TX_STATUS_STOPPED (1 << 0)

struct hfc_sys_chan_tx
{
	struct ks_chan ks_chan;

	struct hfc_hdlc_framer hdlc_framer;
	struct hfc_octet_reverser octet_reverser;

	struct hfc_sys_chan *chan;

	struct hfc_fifo fifo;

	unsigned long status;

	int fifo_pos;
	int fifo_size;
	int fifo_cycles;
	int fifo_min;
	int fifo_max;

	struct tasklet_struct wake_tasklet;
};

struct hfc_sys_port;
struct hfc_sys_chan {
	struct hfc_sys_port *port;

	int id;

	struct ks_duplex ks_duplex;

	struct hfc_sys_chan_rx rx;
	struct hfc_sys_chan_tx tx;
};

struct hfc_sys_chan *hfc_sys_chan_create(
	struct hfc_sys_chan *chan,
	struct hfc_sys_port *port,
	const char *name,
	int id);
void hfc_sys_chan_destroy(struct hfc_sys_chan *chan);

static inline struct hfc_sys_chan *hfc_sys_chan_get(struct hfc_sys_chan *chan)
{
	ks_duplex_get(&chan->ks_duplex);

	return chan;
}

static inline void hfc_sys_chan_put(struct hfc_sys_chan *chan)
{
	ks_duplex_put(&chan->ks_duplex);
}

extern int hfc_sys_chan_register(
	struct hfc_sys_chan *chan);
extern void hfc_sys_chan_unregister(
	struct hfc_sys_chan *chan);

static inline struct hfc_sys_chan_rx *hfc_sys_chan_rx_get(
	struct hfc_sys_chan_rx *chan)
{
	ks_chan_get(&chan->ks_chan);

	return chan;
}

static inline void hfc_sys_chan_rx_put(struct hfc_sys_chan_rx *chan)
{
	ks_chan_put(&chan->ks_chan);
}

static inline struct hfc_sys_chan_tx *hfc_sys_chan_tx_get(
	struct hfc_sys_chan_tx *chan)
{
	ks_chan_get(&chan->ks_chan);

	return chan;
}

static inline void hfc_sys_chan_tx_put(struct hfc_sys_chan_tx *chan)
{
	ks_chan_put(&chan->ks_chan);
}

#endif

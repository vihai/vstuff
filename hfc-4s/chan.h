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

#ifndef _HFC_CHAN_H
#define _HFC_CHAN_H

#include <visdn.h>

#include "util.h"

#define to_chan_duplex(chan) container_of(chan, struct hfc_chan_duplex, visdn_chan)

#ifdef DEBUG_CODE
#define hfc_debug_chan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st%d:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pcidev->dev.bus->name,	\
			(chan)->port->card->pcidev->dev.bus_id,		\
			(chan)->port->id,				\
			(chan)->name,					\
			## arg)

#define hfc_debug_schan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st%d:"						\
			"chan[%s,%s] "					\
			format,						\
			(chan)->chan->port->card->pcidev->dev.bus->name,\
			(chan)->chan->port->card->pcidev->dev.bus_id,	\
			(chan)->chan->port->id,				\
			(chan)->chan->name,				\
			(chan)->direction == RX ? "RX" : "TX",		\
			## arg)
#else
#define hfc_debug_chan(chan, dbglevel, format, arg...) do {} while (0)
#define hfc_debug_schan(schan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st%d:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pcidev->dev.bus->name,		\
		(chan)->port->card->pcidev->dev.bus_id,			\
		(chan)->port->id,					\
		(chan)->name,						\
		## arg)

#define hfc_msg_schan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st%d:"							\
		"chan[%s,%s] "						\
		format,							\
		(chan)->chan->port->card->pcidev->dev.bus->name,	\
		(chan)->chan->port->card->pcidev->dev.bus_id,		\
		(chan)->chan->chan->port->id,				\
		(chan)->chan->chan->name,				\
		(chan)->direction == RX ? "RX" : "TX",			\
		## arg)

struct hfc_chan;
struct hfc_fifo;

struct hfc_chan_simplex
{
	enum hfc_direction direction;

	struct hfc_chan_duplex *chan;
	struct hfc_fifo *fifo;
	struct hfc_pcm_slot *slot;
};

enum hfc_chan_status {
	HFC_CHAN_STATUS_FREE,
	HFC_CHAN_STATUS_OPEN_HDLC,
	HFC_CHAN_STATUS_OPEN_TRANS,
	HFC_CHAN_STATUS_OPEN_BERT,
};

struct hfc_st_port;
struct hfc_chan_duplex {
	struct hfc_st_port *port;

	enum hfc_chan_status status;

	const char *name;
	int id;

	int hw_index;

	struct hfc_chan_simplex rx;
	struct hfc_chan_simplex tx;

	struct visdn_chan visdn_chan;
	struct net_device_stats stats;
};

void hfc_chan_init(
	struct hfc_chan_duplex *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	const int bitrates[],
	int bitrates_cnt);

#endif

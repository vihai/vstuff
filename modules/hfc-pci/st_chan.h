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

#define hfc_D_CHAN_OFF 2
#define hfc_B1_CHAN_OFF 0
#define hfc_B2_CHAN_OFF 1
#define hfc_E_CHAN_OFF 3

struct hfc_st_chan;
struct hfc_fifo;

enum hfc_st_chan_status {
	HFC_CHAN_STATUS_FREE,
	HFC_CHAN_STATUS_OPEN_HDLC,
	HFC_CHAN_STATUS_OPEN_TRANS,
	HFC_CHAN_STATUS_OPEN_E_AUX,
};

struct hfc_st_port;
struct hfc_st_chan {
	struct hfc_st_port *port;

	enum hfc_st_chan_status status;

	int id;

	int hw_index;

	int has_real_fifo;
	struct hfc_fifo rx_fifo;
	struct hfc_fifo tx_fifo;

	struct visdn_chan visdn_chan;

	struct work_struct rx_work;
};

extern void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	int has_real_fifo);

extern int hfc_st_chan_register(struct hfc_st_chan *chan);
extern void hfc_st_chan_unregister(struct hfc_st_chan *chan);

#endif

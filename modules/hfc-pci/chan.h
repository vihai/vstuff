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
	HFC_CHAN_STATUS_OPEN_E_AUX,
};

struct hfc_st_port;
struct hfc_chan_duplex {
	struct hfc_st_port *port;

	enum hfc_chan_status status;

	int id;

	int hw_index;

	struct hfc_chan_simplex rx;
	struct hfc_chan_simplex tx;

	struct visdn_chan visdn_chan;

	int queue_stopped;
};

void hfc_chan_init(
	struct hfc_chan_duplex *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	const int bitrates[],
	int bitrates_cnt);

extern int hfc_chan_sysfs_create_files_D(struct hfc_chan_duplex *chan);
extern int hfc_chan_sysfs_create_files_E( struct hfc_chan_duplex *chan);
extern int hfc_chan_sysfs_create_files_B(struct hfc_chan_duplex *chan);
extern int hfc_chan_sysfs_create_files_SQ(struct hfc_chan_duplex *chan);

extern void hfc_chan_sysfs_delete_files_D(struct hfc_chan_duplex *chan);
extern void hfc_chan_sysfs_delete_files_E(struct hfc_chan_duplex *chan);
extern void hfc_chan_sysfs_delete_files_B(struct hfc_chan_duplex *chan);
extern void hfc_chan_sysfs_delete_files_SQ(struct hfc_chan_duplex *chan);

#endif

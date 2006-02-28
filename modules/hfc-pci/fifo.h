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

#ifndef _HFC_FIFO_H
#define _HFC_FIFO_H

#include "regs.h"
#include "util.h"
#include "module.h"

struct hfc_fifo
{
	struct hfc_card *card;
	struct hfc_st_chan *connected_chan;

	int id;
	enum hfc_direction direction;

	int enabled;
	int bit_reversed;
	int framer_enabled;

	void *z1_base,*z2_base;
	void *mem_base;
	void *z_base;
	u16 z_min;
	u16 z_max;
	u16 size;

	u8 *f1,*f2;
	u8 f_min;
	u8 f_max;
	u8 f_num;

	int stats_max;
	int stats_min;
	int stats_cycles;
};

extern void hfc_fifo_reset(struct hfc_fifo *fifo);
extern int hfc_fifo_get(struct hfc_fifo *fifo, void *data, int size);
extern void hfc_fifo_drop(struct hfc_fifo *fifo, int size);
extern void hfc_fifo_drop_frame(struct hfc_fifo *fifo);

extern void hfc_fifo_mem_read(struct hfc_fifo *fifo,
	void *data,
	int size);
extern int hfc_fifo_mem_read_user(
	struct hfc_fifo *fifo,
	void __user *data,
	int size);
extern void hfc_fifo_mem_read_z(struct hfc_fifo *fifo,
	int z_start,
	void *data,
	int size);

extern void hfc_fifo_mem_write(struct hfc_fifo *fifo,
	const void *data, int size);
extern int hfc_fifo_mem_write_user(
	struct hfc_fifo *fifo,
	const void __user *data,
	int size);

extern int hfc_fifo_is_running(struct hfc_fifo *fifo);
extern void hfc_fifo_configure(struct hfc_fifo *fifo);

extern void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction,
	int base_off,
	int z_off,
	int z1_off, int z2_off,
	int z_min, int z_max,
	int f_min, int f_max,
	int f1_off, int f2_off);

#endif

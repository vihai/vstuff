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

#define hfc_FIFO_D	2
#define hfc_FIFO_B1	0
#define hfc_FIFO_B2	1

#ifdef DEBUG
#define hfc_debug_fifo(fifo, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"fifo[%d,%s]:"				\
			format,					\
			(fifo)->card->pcidev->dev.bus->name,	\
			(fifo)->card->pcidev->dev.bus_id,	\
			(fifo)->hw_index,			\
			(fifo)->direction == RX ? "RX" : "TX",	\
			## arg)
#else
#define hfc_debug_fifo(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_fifo(fifo, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s:"				\
		"fifo[%d,%s]:"				\
		format,					\
		(fifo)->card->pcidev->dev.bus->name,	\
		(fifo)->card->pcidev->dev.bus_id,	\
		(fifo)->hw_index,			\
		(fifo)->direction == RX ? "RX" : "TX",	\
		## arg)

/* NOTE: FIFO pointers are not declared volatile because accesses to the
 *       FIFOs are inherently safe.
 */

struct hfc_fifo
{
	struct hfc_card *card;
	struct hfc_chan_simplex *connected_chan;

	int hw_index;
	enum hfc_direction direction;

	int bit_reversed;

	int used;

	void *z1_base,*z2_base;
	void *fifo_base;
	void *z_base;
	u16 z_min;
	u16 z_max;
	u16 fifo_size;

	u8 *f1,*f2;
	u8 f_min;
	u8 f_max;
	u8 f_num;

	struct work_struct work;
};

void hfc_fifo_clear_rx(struct hfc_fifo *fifo);
void hfc_fifo_clear_tx(struct hfc_fifo *fifo);
int hfc_fifo_get(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_put(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_drop(struct hfc_fifo *fifo, int size);
int hfc_fifo_get_frame(struct hfc_fifo *fifo, void *data, int max_size);
void hfc_fifo_drop_frame(struct hfc_fifo *fifo);
void hfc_fifo_put_frame(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction);
void hfc_fifo_set_bit_order(struct hfc_fifo *fifo, int reversed);

int hfc_fifo_mem_read_user(
	struct hfc_fifo *fifo,
	void __user *data,
	int size);

int hfc_fifo_mem_write_user(
	struct hfc_fifo *fifo,
	const void __user *data,
	int size);


#endif

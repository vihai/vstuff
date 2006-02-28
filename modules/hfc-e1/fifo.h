/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
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

#define hfc_SM_D_FIFO_OFF 2
#define hfc_SM_B1_FIFO_OFF 0
#define hfc_SM_B2_FIFO_OFF 1
#define hfc_SM_E_FIFO_OFF 3

union hfc_zgroup
{
	struct { u16 z1,z2; };
	u32 z1z2;
};

union hfc_fgroup
{
	struct { u8 f1,f2; };
	u16 f1f2;
};

enum hfc_fifo_connect_to
{
	HFC_FIFO_CONNECT_TO_ST,
	HFC_FIFO_CONNECT_TO_PCM,
};

struct hfc_sys_chan;
struct hfc_fifo
{
	struct hfc_sys_chan *chan;

	int hw_index;
	enum hfc_direction direction;

	int subchannel_bit_count;
	int subchannel_bit_start;

	int enabled;
	int bit_reversed;
	int framer_enabled;

	// Counters cache
	union {
		struct { u16 z1,z2; };
		u32 z1z2;
	};

	union {
		struct { u8 f1,f2; };
		u16 f1f2;
	};

	u16 z_min;
	u16 z_max;
	u16 size;

	u8 f_min;
	u8 f_max;
	u8 f_num;

	int stats_max;
	int stats_min;
	int stats_cycles;
};

void hfc_fifo_clear_rx(struct hfc_fifo *fifo);
void hfc_fifo_clear_tx(struct hfc_fifo *fifo);
int hfc_fifo_get(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_put(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_drop(struct hfc_fifo *fifo, int size);
void hfc_fifo_drop_frame(struct hfc_fifo *fifo);
int hfc_fifo_is_running(struct hfc_fifo *fifo);
void hfc_fifo_configure(
	struct hfc_fifo *fifo);
void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_sys_chan *chan,
	int hw_index,
	enum hfc_direction direction);

#endif

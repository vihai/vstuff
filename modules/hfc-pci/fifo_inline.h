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

#ifndef _HFC_FIFO_INLINE_H
#define _HFC_FIFO_INLINE_H

#include "card_inline.h"

static inline u16 *Z1_F1(struct hfc_fifo *fifo)
{
	return fifo->z1_base + (*fifo->f1 * 4);
}

static inline u16 *Z2_F1(struct hfc_fifo *fifo)
{
	return fifo->z2_base + (*fifo->f1 * 4);
}

static inline u16 *Z1_F2(struct hfc_fifo *fifo)
{
	return fifo->z1_base + (*fifo->f2 * 4);
}

static inline u16 *Z2_F2(struct hfc_fifo *fifo)
{
	return fifo->z2_base + (*fifo->f2 * 4);
}

static inline u16 Z_inc(struct hfc_fifo *fifo, u16 z, u16 inc)
{
	// declared as u32 in order to manage overflows
	u32 newz = z + inc;
	if (newz > fifo->z_max)
		newz -= fifo->size;

	return newz;
}

static inline u8 F_inc(struct hfc_fifo *fifo, u8 f, u8 inc)
{
	// declared as u16 in order to manage overflows
	u16 newf = f + inc;
	if (newf > fifo->f_max)
		newf -= fifo->f_num;

	return newf;
}

static inline u16 hfc_fifo_used_rx(struct hfc_fifo *fifo)
{
	return (*Z1_F2(fifo) - *Z2_F2(fifo) + fifo->size) % fifo->size;
}

static inline u16 hfc_fifo_get_frame_size(struct hfc_fifo *fifo)
{
 // This +1 is needed because in frame mode the available bytes are Z2-Z1+1
 // while in transparent mode I wouldn't consider the byte pointed by Z2 to
 // be available, otherwise, the FIFO would always contain one byte, even
 // when Z1==Z2

	return hfc_fifo_used_rx(fifo) + 1;
}

static inline u8 hfc_fifo_u8(struct hfc_fifo *fifo, u16 z)
{
	return *((u8 *)(fifo->z_base + z));
}

static inline u16 hfc_fifo_used_tx(struct hfc_fifo *fifo)
{
	return (*Z1_F1(fifo) - *Z2_F1(fifo) + fifo->size) %
			fifo->size;
}

static inline u16 hfc_fifo_free_tx(struct hfc_fifo *fifo)
{
	int free_bytes = *Z2_F1(fifo) - *Z1_F1(fifo) - 1;

	if (free_bytes >= 0)
		return free_bytes;
	else
		return free_bytes + fifo->size;
}

static inline int hfc_fifo_has_frames(struct hfc_fifo *fifo)
{
	return *fifo->f1 != *fifo->f2;
}

static inline u8 hfc_fifo_free_frames(struct hfc_fifo *fifo)
{
	int free_frames = *fifo->f2 - *fifo->f1 - 1;

	if (free_frames >= 0)
		return free_frames;
	else
		return free_frames + fifo->f_num;
}

#endif

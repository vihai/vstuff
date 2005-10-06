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

#ifndef _HFC_FIFO_INLINE_H
#define _HFC_FIFO_INLINE_H

#include "card_inline.h"

static inline void hfc_fifo_next_frame(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

	hfc_outb(card, hfc_A_INC_RES_FIFO,
		hfc_A_INC_RES_FIFO_V_INC_F);

	hfc_wait_busy(card);
}

static inline u16 Z_inc(struct hfc_fifo *fifo, u16 z, u16 inc)
{
	// declared as u32 in order to manage overflows
	u32 newz = z + inc;
	if (newz > fifo->z_max)
		newz -= fifo->fifo_size;

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
	return (fifo->z1 - fifo->z2 + fifo->fifo_size) % fifo->fifo_size;
}

static inline u16 hfc_fifo_get_frame_size(struct hfc_fifo *fifo)
{
 // This +1 is needed because in frame mode the available bytes are Z2-Z1+1
 // while in transparent mode I wouldn't consider the byte pointed by Z2 to
 // be available, otherwise, the FIFO would always contain one byte, even
 // when Z1==Z2

	return hfc_fifo_used_rx(fifo) + 1;
}

/*
static inline u8 hfc_fifo_u8(struct hfc_fifo *fifo, u16 z)
{
	return *((u8 *)(fifo->z_base + z));
}
*/
static inline u16 hfc_fifo_used_tx(struct hfc_fifo *fifo)
{
	return (fifo->z1 - fifo->z2 + fifo->fifo_size) % fifo->fifo_size;
}

static inline u16 hfc_fifo_free_rx(struct hfc_fifo *fifo)
{
	u16 free_bytes=fifo->z2 - fifo->z1;

	if (free_bytes > 0)
		return free_bytes;
	else
		return free_bytes + fifo->fifo_size;
}

static inline u16 hfc_fifo_free_tx(struct hfc_fifo *fifo)
{
	u16 free_bytes=fifo->z2 - fifo->z1;

	if (free_bytes > 0)
		return free_bytes;
	else
		return free_bytes + fifo->fifo_size;
}

static inline int hfc_fifo_has_frames(struct hfc_fifo *fifo)
{
	return fifo->f1 != fifo->f2;
}

static inline u8 hfc_fifo_used_frames(struct hfc_fifo *fifo)
{
	return (fifo->f1 - fifo->f2 + fifo->f_num) % fifo->f_num;
}

static inline u8 hfc_fifo_free_frames(struct hfc_fifo *fifo)
{
	u8 free_frames = fifo->f2 - fifo->f1;
	if (free_frames > 0)
		return free_frames;
	else
		return free_frames + fifo->f_num;
}

static inline void hfc_fifo_refresh_fz_cache(struct hfc_fifo *fifo)
{
	struct hfc_card *card = card = fifo->card;

	// Se hfc-8s-4s.pdf par 4.4.7 for an explanation of this:
	u16 prev_f1f2 = hfc_inw(card, hfc_A_F12);;
	do {
		fifo->f1f2 = hfc_inw(card, hfc_A_F12);
	} while(fifo->f1f2 != prev_f1f2);

	fifo->z1z2 = hfc_inl(card, hfc_A_Z12);

	rmb(); // Is this barrier really needed?
}

// This function and all subsequent accesses to the selected FIFO must be done
// in interrupt handler or inside a spin_lock_irq* protected section
static inline void hfc_fifo_select(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

	hfc_outb(card, hfc_R_FIFO,
		hfc_R_FIFO_V_FIFO_NUM(fifo->hw_index) |
		(fifo->direction == TX ?
		  hfc_R_FIFO_V_FIFO_DIR_TX :
		  hfc_R_FIFO_V_FIFO_DIR_RX) |
		(fifo->bit_reversed ? hfc_R_FIFO_V_REV : 0));

	mb(); // Be sure actually select the FIFO before waiting

	hfc_wait_busy(card);
	hfc_fifo_refresh_fz_cache(fifo);
}

static inline void hfc_fifo_reset(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

//	hfc_outb(card, hfc_A_INC_RES_FIFO,
//		hfc_A_INC_RES_FIFO_V_RES_F);

        hfc_wait_busy(card);
}

#endif

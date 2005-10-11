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
	u16 prev_f1f2 = hfc_inw(card, hfc_A_F12);
	do {
		fifo->f1f2 = hfc_inw(card, hfc_A_F12);
	} while(fifo->f1f2 != prev_f1f2);

	fifo->z1z2 = hfc_inl(card, hfc_A_Z12);

	rmb(); // Is this barrier really needed?
}

// This function and all subsequent accesses to the selected FIFO must be done
// in atomic context
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

	/* We may read the counters in a 125us frame after FIFO selection
	 * without fearing of them being updated in the while as per
	 * docs §4.4.7
	 */
	fifo->f1f2 = hfc_inw(card, hfc_A_F12);
	fifo->z1z2 = hfc_inl(card, hfc_A_Z12);
}

static inline void hfc_fifo_reset(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

//	hfc_outb(card, hfc_A_INC_RES_FIFO,
//		hfc_A_INC_RES_FIFO_V_RES_F);

        hfc_wait_busy(card);
}

static inline int hfc_fifo_mem_read(
	struct hfc_fifo *fifo,
	void *data, int size)
{
	struct hfc_card *card = fifo->card;

#if 0
	int i;
	for (i=0; i<size; i++) {
		((u8 *)data)[i] = hfc_inb(card, hfc_A_FIFO_DATA0);
	}
#endif

	union { u32 l; u8 b[4]; } word;

	// Would unaligned write be better?
	int pos = 0;
	while (pos < size - 3) {
		word.l = hfc_inl(card, hfc_A_FIFO_DATA2);
		memcpy(data + pos, word.b, 4);
		pos += 4;
	}

	while (pos < size) {
		*((u8 *)(data + pos)) = hfc_inb(card, hfc_A_FIFO_DATA0);
		pos++;
	}

	return size;
}

static inline int hfc_fifo_mem_read_dword(
	struct hfc_fifo *fifo,
	void *data, int size)
{
	struct hfc_card *card = fifo->card;

	union { u32 l; u8 b[4]; } word;

	// Would unaligned write be better?
	int pos = 0;
	while (pos < size - 3) {
		word.l = hfc_inl(card, hfc_A_FIFO_DATA2);
		memcpy(data + pos, word.b, 4);
		pos += 4;
	}

	return pos;
}

static inline int hfc_fifo_mem_read_to_user(
	struct hfc_fifo *fifo,
	void __user *data, int size)
{
	struct hfc_card *card = fifo->card;
	int err;

	int i;
	for (i=0; i<size; i++) {
		err = put_user(hfc_inb(card, hfc_A_FIFO_DATA0),
				(u8 *)(data + i));

		if (err < 0)
			return err;
	}

	return size;
}

static inline void hfc_fifo_mem_write(
	struct hfc_fifo *fifo,
	const void *data, int size)
{
	struct hfc_card *card = fifo->card;

#if 0
	int i;
	for (i=0; i<size; i++) {
		hfc_outb(card,
			hfc_A_FIFO_DATA0,
			((u8 *)data)[i]);
	}
#endif

	union { u32 l; u8 b[4]; } word;

	int pos = 0;
	while (pos < size - 3) {
		memcpy(word.b, data + pos, 4);
		hfc_outl(card, hfc_A_FIFO_DATA2, word.l);
		pos += 4;
	}

	while (pos < size) {
		hfc_outb(card, hfc_A_FIFO_DATA0, *((u8 *)data + pos));
		pos++;
	}
}

static inline void hfc_fifo_mem_write_from_user(
	struct hfc_fifo *fifo,
	const void __user *data, int size)
{
	struct hfc_card *card = fifo->card;

	int i;
	for (i=0; i<size; i++) {
		u8 val;
		get_user(val, (u8 *)(data + i));

		hfc_outb(card,
			hfc_A_FIFO_DATA0,
			val);
	}
}

#endif

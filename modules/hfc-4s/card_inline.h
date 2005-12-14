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

#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include <linux/interrupt.h>

#include "card.h"
#include "module.h"

static inline void hfc_card_lock(struct hfc_card *card)
{
	spin_lock_bh(&card->lock);
}

static inline int hfc_card_trylock(struct hfc_card *card)
{
	return spin_trylock(&card->lock);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
/*	card->st_port_selected = NULL;
	card->fifo_selected = NULL;
	card->pcm_multireg = -1;
	card->pcm_slot_selected = NULL;*/

	spin_unlock_bh(&card->lock);
}

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
	return ioread8(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
	iowrite8(value, card->io_mem + offset);
}

static inline u16 hfc_inw(struct hfc_card *card, int offset)
{
	return ioread16(card->io_mem + offset);
}

static inline void hfc_outw(struct hfc_card *card, int offset, u16 value)
{
	iowrite16(value, card->io_mem + offset);
}

static inline u32 hfc_inl(struct hfc_card *card, int offset)
{
	return ioread32(card->io_mem + offset);
}

static inline void hfc_outl(struct hfc_card *card, int offset, u32 value)
{
	iowrite32(value, card->io_mem + offset);
}

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<5000; i++) {
		if (!(hfc_inb(card, hfc_R_STATUS) & hfc_R_STATUS_V_BUSY))
			return;

		cpu_relax();
	}

	hfc_msg_card(card, KERN_ERR, hfc_DRIVER_PREFIX
		"Stuck in busy state...\n");
}

#endif

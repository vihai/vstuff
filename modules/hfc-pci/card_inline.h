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

#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include <linux/spinlock.h>
#include <asm/io.h>

#include "card.h"

static inline void hfc_card_lock(struct hfc_card *card)
{
	spin_lock_bh(&card->lock);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
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

#endif

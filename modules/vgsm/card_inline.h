/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_INLINE_H
#define _VGSM_INLINE_H

#include <asm/uaccess.h>

#include "card.h"

static inline unsigned int vgsm_inb(struct vgsm_card *card, int offset)
{
	return ioread8(card->io_mem + offset);
}

static inline void vgsm_outb(struct vgsm_card *card, int offset, u8 value)
{
	iowrite8(value, card->io_mem + offset);
}

static inline unsigned int vgsm_inl(struct vgsm_card *card, int offset)
{
	return ioread32(card->io_mem + offset);
}

static inline void vgsm_outl(struct vgsm_card *card, int offset, u32 value)
{
	iowrite32(value, card->io_mem + offset);
}

static inline void vgsm_card_lock(struct vgsm_card *card)
{
	spin_lock(&card->lock);
}

static inline void vgsm_card_unlock(struct vgsm_card *card)
{
	spin_unlock(&card->lock);
}

#endif

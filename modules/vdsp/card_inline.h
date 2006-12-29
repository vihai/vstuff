/*
 * VoiSmart vDSP board driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_INLINE_H
#define _VGSM_INLINE_H

#include <asm/uaccess.h>

#include "card.h"

static inline unsigned int vdsp_inb(struct vdsp_card *card, int offset)
{
	return ioread8(card->regs_ptr + offset);
}

static inline void vdsp_outb(struct vdsp_card *card, int offset, u8 value)
{
	iowrite8(value, card->regs_ptr + offset);
}

static inline unsigned int vdsp_inl(struct vdsp_card *card, int offset)
{
	return ioread32(card->regs_ptr + offset);
}

static inline void vdsp_outl(struct vdsp_card *card, int offset, u32 value)
{
	iowrite32(value, card->regs_ptr + offset);
}

static inline void vdsp_card_lock(struct vdsp_card *card)
{
	spin_lock_bh(&card->lock);
}

static inline void vdsp_card_unlock(struct vdsp_card *card)
{
	spin_unlock_bh(&card->lock);
}

#endif

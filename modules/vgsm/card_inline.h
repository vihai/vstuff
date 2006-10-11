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

#include <linux/delay.h>
#include <asm/uaccess.h>

#include "card.h"
#include "regs.h"

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

//#define vgsm_card_lock(card)	spin_lock_irqsave(&card->lock, irqflags);

static inline void vgsm_card_lock(struct vgsm_card *card)
{
	spin_lock_bh(&card->lock);
}

static inline void vgsm_card_unlock(struct vgsm_card *card)
{
	spin_unlock_bh(&card->lock);
}

static inline void vgsm_wait_e0(struct vgsm_card *card)
{
	int i;

//	if (vgsm_inb(card, VGSM_PIB_E0))
//		printk(KERN_DEBUG "E0 != 0 !!!\n");

	for (i=0; i<100; i++) {

		if (!vgsm_inb(card, VGSM_PIB_E0))
			return;

		udelay(10);
	}

	vgsm_msg(KERN_ERR, "Timeout waiting for E0 buffer\n");
}

/* Send interrupt to micros - 0x01 for Micro 0, 0x02 for micro 1 */
static inline void vgsm_interrupt_micro(struct vgsm_card *card, 
	u8 value)
{
	mb();
	vgsm_outb(card, VGSM_PIB_E4, value);
}

#endif

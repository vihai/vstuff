#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include <linux/pci.h>

#include "card.h"
#include "module.h"

static inline void hfc_card_lock(struct hfc_card *card)
{
	down(&card->sem);
}

static inline int hfc_card_trylock(struct hfc_card *card)
{
	return down_trylock(&card->sem);
}

static inline int hfc_card_lock_interruptible(struct hfc_card *card)
{
	return down_interruptible(&card->sem);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
/*	card->st_port_selected = NULL;
	card->fifo_selected = NULL;
	card->pcm_multireg = -1;
	card->pcm_slot_selected = NULL;*/

	up(&card->sem);
}

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
	return readb(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
	writeb(value, card->io_mem + offset);
}

static inline u16 hfc_inw(struct hfc_card *card, int offset)
{
	return readw(card->io_mem + offset);
}

static inline void hfc_outw(struct hfc_card *card, int offset, u16 value)
{
	writew(value, card->io_mem + offset);
}

static inline u32 hfc_inl(struct hfc_card *card, int offset)
{
	return readl(card->io_mem + offset);
}

static inline void hfc_outl(struct hfc_card *card, int offset, u32 value)
{
	writel(value, card->io_mem + offset);
}

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<100; i++) {
		if (!(hfc_inb(card, hfc_R_STATUS) & hfc_R_STATUS_V_BUSY))
			return;

		udelay(1);
	}

	hfc_msg_card(card, KERN_ERR, hfc_DRIVER_PREFIX
		"Stuck in busy state...\n");
}

#endif

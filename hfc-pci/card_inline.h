#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include <linux/pci.h>

#include "card.h"

static inline void hfc_card_lock(struct hfc_card *card)
{
	down(&card->sem);
}

static inline int hfc_card_lock_interruptible(struct hfc_card *card)
{
	return down_interruptible(&card->sem);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
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

#endif

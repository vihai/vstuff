#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include <linux/pci.h>

#include "card.h"

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
	return readb(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
	writeb(value, card->io_mem + offset);
}

#endif

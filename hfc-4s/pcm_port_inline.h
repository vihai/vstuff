#ifndef _HFC_PCM_PORT_INLINE_H
#define _HFC_PCM_PORT_INLINE_H

#include <linux/pci.h>

#include "card_inline.h"

static inline void hfc_pcm_slot_select(struct hfc_card *card, u8 id)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	hfc_outb(card, hfc_R_SLOT, id);

	mb();
}

#endif

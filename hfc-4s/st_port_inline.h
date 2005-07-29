#ifndef _HFC_ST_PORT_INLINE_H
#define _HFC_ST_PORT_INLINE_H

#include <linux/pci.h>

#include "card_inline.h"

static inline void hfc_st_port_select(struct hfc_card *card, u8 id)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	mb();
	hfc_outb(card, hfc_R_ST_SEL,
		hfc_R_ST_SEL_V_ST_SEL(id));
	mb();
}

#endif

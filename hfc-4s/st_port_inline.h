#ifndef _HFC_ST_PORT_INLINE_H
#define _HFC_ST_PORT_INLINE_H

#include <linux/pci.h>

#include "card_inline.h"

static inline void hfc_st_port_select(struct hfc_st_port *port)
{
	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	mb();
	hfc_outb(port->card, hfc_R_ST_SEL,
		hfc_R_ST_SEL_V_ST_SEL(port->id));
	mb();
}

#endif

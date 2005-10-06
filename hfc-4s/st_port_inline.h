/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_ST_PORT_INLINE_H
#define _HFC_ST_PORT_INLINE_H

#include <linux/pci.h>

#include "card_inline.h"

static inline void hfc_st_port_select(struct hfc_st_port *port)
{
	WARN_ON(!hfc_card_locked(port->card));

/*	card->st_port_selected = port;
	card->fifo_selected = NULL;
	card->pcm_slot_selected = NULL;
	card->pcm_multireg = -1;*/

	mb();
	hfc_outb(port->card, hfc_R_ST_SEL,
		hfc_R_ST_SEL_V_ST_SEL(port->id));
	mb();
}

#endif

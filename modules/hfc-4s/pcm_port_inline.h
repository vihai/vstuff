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

#ifndef _HFC_PCM_PORT_INLINE_H
#define _HFC_PCM_PORT_INLINE_H

#include <linux/pci.h>

#include "card.h"

static inline void hfc_pcm_port_multireg_select(struct hfc_card *card, u8 id)
{
	mb();
	hfc_card_update_pcm_md0(card, id);
	hfc_wait_busy(card);
	mb();
}

static inline void hfc_pcm_port_select_slot(struct hfc_card *card, u8 id)
{
	mb();
	hfc_outb(card, hfc_R_SLOT, id);
	hfc_wait_busy(card);
	mb();
}

static inline struct hfc_pcm_port *hfc_pcm_port_get(struct hfc_pcm_port *port)
{
	visdn_port_get(&port->visdn_port);

	return port;
}

static inline void hfc_pcm_port_put(struct hfc_pcm_port *port)
{
	visdn_port_put(&port->visdn_port);
}

#endif

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
#include "card_inline.h"

static inline void hfc_pcm_slot_select(struct hfc_card *card, u8 id)
{
	WARN_ON(atomic_read(&card->sem.count) > 0);

/*	card->st_port_selected = NULL;
	card->fifo_selected = NULL;
	card->pcm_slot_selected = slot;
	card->pcm_multireg = -1; */

	mb();
	hfc_outb(card, hfc_R_SLOT, id);
	hfc_wait_busy(card);
	mb();
}

static inline void hfc_pcm_multireg_select(struct hfc_card *card, u8 id)
{
	WARN_ON(atomic_read(&card->sem.count) > 0);

/*	card->st_port_selected = NULL;
	card->fifo_selected = NULL;
	card->pcm_slot_selected = NULL;
	card->pcm_multireg = id; */

	mb();
	hfc_update_pcm_md0(card, id);
	hfc_wait_busy(card);
	mb();
}

#endif

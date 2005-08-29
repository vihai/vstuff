/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include "st_port.h"
#include "card.h"
#include "card_inline.h"

struct hfc_pcm_slot *hfc_pcm_port_allocate_slot(
	struct hfc_pcm_port *port,
	enum hfc_direction direction)
{
	int i;
	for (i=0; i<port->num_slots; i++) {
		if (!port->slots[i][direction].used) {
			port->slots[i][direction].used = TRUE;
			return &port->slots[i][direction];
		}
	}

	return NULL;
}

void hfc_pcm_port_deallocate_slot(struct hfc_pcm_slot *slot)
{
	slot->used = FALSE;
}

static int hfc_pcm_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;
	hfc_card_unlock(card);

	hfc_debug_pcm_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_pcm_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;
	hfc_card_unlock(card);

	hfc_debug_pcm_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_pcm_port_ops = {
	.enable		= hfc_pcm_port_enable,
	.disable	= hfc_pcm_port_disable,
};

static inline void hfc_pcm_port_slot_init(
	struct hfc_pcm_slot *slot,
	struct hfc_pcm_port *port,
	int hw_index,
	enum hfc_direction direction)
{
	slot->port = port;
	slot->hw_index = hw_index;
	slot->direction = direction;
}

void hfc_pcm_port_init(
	struct hfc_pcm_port *port,
	struct hfc_card *card)
{
	port->card = card;
	visdn_port_init(&port->visdn_port, &hfc_pcm_port_ops);

	int i;
	for (i=0; i<sizeof(port->slots)/sizeof(*port->slots); i++) {
		hfc_pcm_port_slot_init(&port->slots[i][RX], port, i, RX);
		hfc_pcm_port_slot_init(&port->slots[i][TX], port, i, TX);
	}
}


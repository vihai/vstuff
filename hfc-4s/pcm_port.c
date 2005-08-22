#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "st_port.h"
#include "card.h"
#include "card_inline.h"

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

void hfc_pcm_port_init(struct hfc_pcm_port *port)
{
	int i;
	for (i=0; i<sizeof(port->slots)/sizeof(*port->slots); i++) {
		hfc_pcm_port_slot_init(&port->slots[i][RX], port, i, RX);
		hfc_pcm_port_slot_init(&port->slots[i][TX], port, i, TX);
	}
}

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

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	up(&port->card->sem);

	hfc_debug_pcm_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_pcm_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	up(&port->card->sem);

	hfc_debug_pcm_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_pcm_port_ops = {
	.set_role	= NULL,
	.enable		= hfc_pcm_port_enable,
	.disable	= hfc_pcm_port_disable,
};


#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "hfc-4s.h"
#include "st_port.h"
#include "card.h"
#include "card_inline.h"

static int hfc_pcm_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);


	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_pcm_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_pcm_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_pcm_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_pcm_port_ops = {
	.set_role	= NULL,
	.enable		= hfc_pcm_port_enable,
	.disable	= hfc_pcm_port_disable,
};


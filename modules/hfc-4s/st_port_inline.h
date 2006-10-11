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

#include "card.h"

static inline struct hfc_st_port *hfc_st_port_get(struct hfc_st_port *port)
{
	visdn_port_get(&port->visdn_port);

	return port;
}

static inline void hfc_st_port_put(struct hfc_st_port *port)
{
	visdn_port_put(&port->visdn_port);
}

static inline void hfc_st_port_select(struct hfc_st_port *port)
{
	mb();
	hfc_outb(port->card, hfc_R_ST_SEL,
		hfc_R_ST_SEL_V_ST_SEL(port->id));
	mb();
	hfc_wait_busy(port->card);
	mb();
}

#endif

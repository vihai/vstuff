/*
 * Cologne Chip's HFC-USB vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_FIFO_INLINE_H
#define _HFC_FIFO_INLINE_H

#include "fifo.h"
#include "card_inline.h"

/*
 * This function and all subsequent accesses to the selected FIFO must be done
 * atomically.
 */

static inline void hfc_fifo_select(struct hfc_fifo *fifo)
{
	hfc_write(fifo->chan->port->card, HFC_REG_FIFO, fifo->hw_index);
}

#endif

/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>

#include "fifo.h"
#include "fifo_inline.h"
#include "card.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
#define dev_name(&((fifo)->card->pci_dev->dev)) (fifo)->card->pci_dev->dev.bus_id
#endif

#ifdef DEBUG_CODE
#define hfc_debug_fifo(fifo, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"fifo[%d,%s]:"					\
			format,						\
			(fifo)->card->pci_dev->dev.bus->name,\
			dev_name(&((fifo)->card->pci_dev->dev)),		\
			(fifo)->hw_index,				\
			(fifo)->direction == RX ? "RX" : "TX",		\
			## arg)
#else
#define hfc_debug_fifo(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_fifo(fifo, level, format, arg...)		\
	printk(level hfc_DRIVER_PREFIX				\
		"%s-%s:"					\
		"fifo[%d,%s]:"					\
		format,						\
		(fifo)->card->pci_dev->dev.bus->name,\
		dev_name(&((fifo)->card->pci_dev->dev)),		\
		(fifo)->hw_index,				\
		(fifo)->direction == RX ? "RX" : "TX",		\
		## arg)


void hfc_fifo_drop(struct hfc_fifo *fifo, int size)
{
	int available_bytes = hfc_fifo_used(fifo);
	if (available_bytes + 1 < size) {
		hfc_msg_fifo(fifo, KERN_WARNING,
			"RX FIFO not enough (%d) bytes to drop!\n",
			available_bytes);

		return;
	}

	// FIXME read and drop bytes
}

void hfc_fifo_drop_frame(struct hfc_fifo *fifo)
{
	// FIXME read and drop all the frame

	hfc_fifo_next_frame(fifo);
}

void hfc_fifo_configure(
	struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;
	u8 con_hdlc = 0;

	if (!fifo->enabled) {
		hfc_outb(card, hfc_A_CON_HDLC,
				hfc_A_CON_HDCL_V_HDLC_TRP_HDLC |
				hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED);
		hfc_outb(card, hfc_A_IRQ_MSK, 0);
		return;
	}

	if (fifo->framer_enabled) {
		con_hdlc |= hfc_A_CON_HDCL_V_HDLC_TRP_HDLC |
			    hfc_A_CON_HDCL_V_IFF |
			    hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED;
	} else {
		con_hdlc |= hfc_A_CON_HDCL_V_HDLC_TRP_TRP |
			    hfc_A_CON_HDCL_V_TRP_IRQ_DISABLED;
	}

	if (fifo->direction == RX) {
//		if (fifo->connect_to == HFC_FIFO_CONNECT_TO_ST)
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST;
//			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_PCM;
//		else
//			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST_ST_from_PCM;
	} else {
//		if (fifo->connect_to == HFC_FIFO_CONNECT_TO_ST)
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM;
//		else
//			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_ST_to_PCM;
	}

	hfc_outb(card, hfc_A_CON_HDLC, con_hdlc);
	hfc_outb(card, hfc_A_IRQ_MSK, hfc_A_IRQ_MSK_V_IRQ);
}

void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction)
{
	fifo->enabled = FALSE;
	fifo->card = card;
	fifo->hw_index = hw_index;
	fifo->direction = direction;

	fifo->framer_enabled = FALSE;
	fifo->subchannel_bit_count = 8;
	fifo->subchannel_bit_start = 0;

}

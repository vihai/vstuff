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

#include <linux/visdn/core.h>

#include "card.h"
#include "fifo.h"
#include "fifo_inline.h"

#ifdef DEBUG_CODE
#define hfc_debug_fifo(fifo, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"fifo[%d,%s]:"				\
			format,					\
			(fifo)->card->pci_dev->dev.bus->name,	\
			(fifo)->card->pci_dev->dev.bus_id,	\
			(fifo)->id,				\
			(fifo)->direction == RX ? "RX" : "TX",	\
			## arg)
#else
#define hfc_debug_fifo(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_fifo(fifo, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s:"				\
		"fifo[%d,%s]:"				\
		format,					\
		(fifo)->card->pci_dev->dev.bus->name, \
		(fifo)->card->pci_dev->dev.bus_id,	\
		(fifo)->id,			\
		(fifo)->direction == RX ? "RX" : "TX",	\
		## arg)

void hfc_fifo_reset(struct hfc_fifo *fifo)
{
	int i;
	for (i=fifo->f_min; i<=fifo->f_max; i++) {
		*fifo->f2 = fifo->f_max;
		*fifo->f1 = fifo->f_max;

		*Z2_F2(fifo) = fifo->z_max;
		*Z1_F2(fifo) = fifo->z_max;
	}
}

void hfc_fifo_mem_read(struct hfc_fifo *fifo,
	void *data,
	int size)
{
	int octets_to_boundary = fifo->z_max - *Z2_F2(fifo) + 1;
	if (octets_to_boundary >= size) {
		memcpy(data,
			fifo->z_base + *Z2_F2(fifo),
			size);
	} else {
		// Buffer wrap
		memcpy(data,
			fifo->z_base + *Z2_F2(fifo),
			octets_to_boundary);

		memcpy(data + octets_to_boundary,
			fifo->mem_base,
			size - octets_to_boundary);
	}
}

void hfc_fifo_mem_read_z(struct hfc_fifo *fifo,
	int z_start,
	void *data,
	int size)
{
	int octets_to_boundary = fifo->z_max - z_start + 1;
	if (octets_to_boundary >= size) {
		memcpy(data,
			fifo->z_base + z_start,
			size);
	} else {
		// Buffer wrap
		memcpy(data,
			fifo->z_base + z_start,
			octets_to_boundary);

		memcpy(data + octets_to_boundary,
			fifo->mem_base,
			size - octets_to_boundary);
	}
}

void hfc_fifo_mem_write(
	struct hfc_fifo *fifo,
	const void *data, int size)
{
	int octets_to_boundary = fifo->z_max - *Z1_F1(fifo) + 1;
	if (octets_to_boundary >= size) {
		memcpy(fifo->z_base + *Z1_F1(fifo),
			data,
			size);
	} else {
		// FIFO wrap

		memcpy(fifo->z_base + *Z1_F1(fifo),
			data,
			octets_to_boundary);

		memcpy(fifo->mem_base,
			data + octets_to_boundary,
			size - octets_to_boundary);
	}
}

int hfc_fifo_mem_read_user(
	struct hfc_fifo *fifo,
	void __user *buf,
	int size)
{
	int octets_to_boundary = fifo->z_max - *Z2_F2(fifo) + 1;
	if (octets_to_boundary >= size) {
		if (copy_to_user(buf, fifo->z_base + *Z2_F2(fifo), size))
			return -EFAULT;
	} else {
		// FIFO wrap

		if (copy_to_user(buf, fifo->z_base + *Z2_F2(fifo), octets_to_boundary))
			return -EFAULT;

		if (copy_to_user(buf + octets_to_boundary,
				fifo->mem_base,
				size - octets_to_boundary))
			return -EFAULT;
	}

	return 0;
}

int hfc_fifo_mem_write_user(
	struct hfc_fifo *fifo,
	const void __user *buf,
	int size)
{
	int octets_to_boundary = fifo->z_max - *Z1_F1(fifo) + 1;
	if (octets_to_boundary >= size) {
		if (copy_from_user(fifo->z_base + *Z1_F1(fifo), buf, size))
			return -EFAULT;
	} else {
		// FIFO wrap

		if (copy_from_user(fifo->z_base + *Z1_F1(fifo),
				buf,
				octets_to_boundary))
			return -EFAULT;

		if (copy_from_user(fifo->mem_base,
				buf + octets_to_boundary,
				size - octets_to_boundary))
			return -EFAULT;
	}

	return 0;
}

void hfc_fifo_drop(struct hfc_fifo *fifo, int size)
{
	int available_octets = hfc_fifo_used_rx(fifo);
	if (available_octets + 1 < size) {
		hfc_msg_fifo(fifo, KERN_WARNING,
			"RX FIFO not enough (%d) octets to drop!\n",
			available_octets);

		return;
	}

	*Z2_F2(fifo) = Z_inc(fifo, *Z2_F2(fifo), size);
}

void hfc_fifo_drop_frame(struct hfc_fifo *fifo)
{
	int available_octets;

	if (*fifo->f1 == *fifo->f2) {
		// nothing received, strange eh?
		hfc_msg_fifo(fifo, KERN_WARNING,
			"skip_frame called with no frame in FIFO.\n");

		return;
	}

//	fifo->drops++;

	available_octets = hfc_fifo_used_rx(fifo) + 1;

	{
	// Calculate beginning of the next frame
	u16 newz2 = Z_inc(fifo, *Z2_F2(fifo), available_octets);

	*fifo->f2 = F_inc(fifo, *fifo->f2, 1);

	// Set Z2 for the next frame we're going to receive
	*Z2_F2(fifo) = newz2;
	}
}

int hfc_fifo_is_running(struct hfc_fifo *fifo)
{
	if (fifo->enabled &&
	    fifo->connected_chan &&
	    (((fifo->connected_chan->port->nt_mode &&
	      fifo->connected_chan->port->l1_state == 3) ||
	     (!fifo->connected_chan->port->nt_mode &&
	      fifo->connected_chan->port->l1_state == 7)))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void hfc_fifo_configure(
	struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

	switch(fifo->id) {
	case D:
		card->regs.mst_emod &= ~hfc_MST_EMOD_D_MASK;
		card->regs.mst_emod |=
			hfc_MST_EMOD_D_HFC_from_ST |
			hfc_MST_EMOD_D_ST_from_HFC |
			hfc_MST_EMOD_D_GCI_from_HFC;

		hfc_outb(card, hfc_MST_EMOD, card->regs.mst_emod);

		if (hfc_fifo_is_running(fifo))
			card->regs.fifo_en |= hfc_FIFO_EN_D;
		else
			card->regs.fifo_en &= ~hfc_FIFO_EN_D;

		card->regs.fifo_en |= hfc_FIFO_EN_DRX;
		card->regs.m1 |= hfc_INT_M1_DREC | hfc_INT_M1_DTRANS;
	break;

	case B1:
		if (fifo->framer_enabled)
			card->regs.ctmt &= ~hfc_CTMT_TRANSB1;
		else
			card->regs.ctmt |= hfc_CTMT_TRANSB1;

		card->regs.connect &= hfc_CONNECT_B1_MASK;
		card->regs.connect |=
			hfc_CONNECT_B1_HFC_from_ST |
			hfc_CONNECT_B1_ST_from_HFC |
			hfc_CONNECT_B1_GCI_from_HFC;

		if (fifo->bit_reversed)
			card->regs.cirm |= hfc_CIRM_B1_REV;
		else
			card->regs.cirm &= ~hfc_CIRM_B1_REV;

		if (hfc_fifo_is_running(fifo))
			card->regs.fifo_en |= hfc_FIFO_EN_B1;
		else
			card->regs.fifo_en &= ~hfc_FIFO_EN_B1;
	break;

	case B2:
		if (fifo->framer_enabled)
			card->regs.ctmt &= ~hfc_CTMT_TRANSB2;
		else
			card->regs.ctmt |= hfc_CTMT_TRANSB2;

		card->regs.connect &= hfc_CONNECT_B2_MASK;
		card->regs.connect |=
			hfc_CONNECT_B2_HFC_from_ST |
			hfc_CONNECT_B2_ST_from_HFC |
			hfc_CONNECT_B2_GCI_from_HFC;

		if (fifo->bit_reversed)
			card->regs.cirm |= hfc_CIRM_B2_REV;
		else
			card->regs.cirm &= ~hfc_CIRM_B2_REV;

		if (hfc_fifo_is_running(fifo))
			card->regs.fifo_en |= hfc_FIFO_EN_B2;
		else
			card->regs.fifo_en &= ~hfc_FIFO_EN_B2;
	break;
	}

	hfc_outb(card, hfc_CTMT, card->regs.ctmt);
	hfc_outb(card, hfc_CIRM, card->regs.cirm);

	hfc_outb(card, hfc_FIFO_EN, card->regs.fifo_en);
	hfc_outb(card, hfc_CONNECT, card->regs.connect);
//	hfc_outb(card, hfc_TRM, card->regs.trm);
}

void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int id,
	enum hfc_direction direction,
	int base_off,
	int z_off,
	int z1_off, int z2_off,
	int z_min, int z_max,
	int f_min, int f_max,
	int f1_off, int f2_off)
{
	fifo->id = id;
	fifo->direction = direction;
	fifo->card = card;

	fifo->mem_base	= card->fifo_mem + base_off;
	fifo->z_base    = card->fifo_mem + z_off;
	fifo->z1_base   = card->fifo_mem + z1_off;
	fifo->z2_base   = card->fifo_mem + z2_off;
	fifo->z_min 	= z_min;
	fifo->z_max 	= z_max;
	fifo->f_min	= f_min;
	fifo->f_max	= f_max;
	fifo->f1	= card->fifo_mem + f1_off;
	fifo->f2	= card->fifo_mem + f2_off;
	fifo->size	= fifo->z_max - fifo->z_min + 1;
	fifo->f_num	= fifo->f_max - fifo->f_min + 1;
}

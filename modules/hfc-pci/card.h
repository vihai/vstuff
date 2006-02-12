/*
 * vISDN HFC-S PCI A driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_CARD_H
#define _HFC_CARD_H

#include <linux/delay.h>
#include <linux/pci.h>

#include "st_port.h"
#include "pcm_port.h"
#include "fifo.h"
#include "regs.h"

#ifdef DEBUG_CODE
#define hfc_debug_card(card, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"%s-%s: "			\
			format,				\
			(card)->pci_dev->dev.bus->name,	\
			(card)->pci_dev->dev.bus_id,	\
			## arg)
#else
#define hfc_debug_card(card, dbglevel, format, arg...)	\
	do { card = card; } while (0)
#endif

#define hfc_msg_card(card, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s: "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)

#define hfc_FIFO_B1 0
#define hfc_FIFO_B2 1
#define hfc_FIFO_D 2

struct hfc_card
{
	spinlock_t lock;

	struct pci_dev *pci_dev;

	unsigned long io_bus_mem;
	void __iomem *io_mem;

	dma_addr_t fifo_bus_mem;
	void *fifo_mem;

	int sync_loss_reported;

	struct {
		u8 m1;
		u8 m2;
		u8 fifo_en;
		u8 trm;
		u8 cirm;

		u8 sctrl_e;

		u8 connect;
		u8 ctmt;

		u8 mst_emod;
	} regs;

	struct hfc_st_port st_port;
	struct hfc_pcm_port pcm_port;

	struct hfc_fifo fifos[3][2];

	int debug_event;
};

void hfc_softreset(struct hfc_card *card);
void hfc_initialize_hw(struct hfc_card *card);

int __devinit hfc_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry);
void __devexit hfc_card_remove(struct hfc_card *card);

void hfc_card_fifo_update(struct hfc_card *card);

#endif

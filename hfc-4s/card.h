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

#ifndef _HFC_CARD_H
#define _HFC_CARD_H

#include <linux/delay.h>
#include <linux/pci.h>

#include <visdn.h>

#include "st_port.h"
#include "pcm_port.h"
#include "fifo.h"
#include "regs.h"
//#include "fifo.h"

#ifdef DEBUG_CODE
#define hfc_debug_card(card, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"%s-%s "			\
			format,				\
			(card)->pcidev->dev.bus->name,	\
			(card)->pcidev->dev.bus_id,	\
			## arg)
#else
#define hfc_debug_card(card, dbglevel, format, arg...)	\
	do { card = card; } while (0)
#endif

#define hfc_msg_card(card, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s "				\
		format,					\
		(card)->pcidev->dev.bus->name,		\
		(card)->pcidev->dev.bus_id,		\
		## arg)

struct hfc_card {
	struct semaphore sem;

	// This struct contains a copy of some registers whose bits may be
	// changed independently.
	struct
	{
		u8 ram_misc;
		u8 ctrl;
		u8 fifo_md;
		u8 irqmsk_misc;
	} regs;

	struct pci_dev *pcidev;

	int num_st_ports;
	struct hfc_st_port st_ports[8];
	struct hfc_st_port *st_port_selected;

	struct hfc_pcm_port pcm_port;

	int num_fifos;
	struct hfc_fifo fifos[32][2];
	struct hfc_fifo *fifo_selected;

	unsigned long io_bus_mem;
	void *io_mem;

	int clock_source;
	int ram_size;
	int bert_mode;
	int output_level;
};

struct hfc_fifo *hfc_allocate_fifo(
	struct hfc_card *card,
	enum hfc_direction direction);
void hfc_deallocate_fifo(struct hfc_fifo *fifo);

void hfc_configure_fifos(
	struct hfc_card *card,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz);

void hfc_softreset(struct hfc_card *card);
void hfc_initialize_hw(struct hfc_card *card);

void hfc_update_pcm_md0(struct hfc_card *card, u8 otherbits);
void hfc_update_pcm_md1(struct hfc_card *card);
void hfc_update_st_sync(struct hfc_card *card);
void hfc_update_bert_wd_md(struct hfc_card *card, u8 otherbits);

#endif

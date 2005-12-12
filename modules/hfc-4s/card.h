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

#include <linux/visdn/core.h>
#include <linux/visdn/cxc.h>

#include "st_port.h"
#include "pcm_port.h"
#include "sys_port.h"
#include "cxc.h"
#include "led.h"
#include "regs.h"

#ifdef DEBUG_CODE
#define hfc_debug_card(card, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"%s-%s "			\
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
		"%s-%s "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)

struct hfc_card_config
{
	int double_clock;
	int quartz_49;
	int ram_size;
};

struct hfc_card {
	spinlock_t lock;

	// This struct contains a copy of some registers whose bits may be
	// changed independently.
	struct
	{
		u8 irqmsk_misc;
	} regs;

	struct pci_dev *pci_dev;

	int double_clock;
	int quartz_49;

	struct hfc_cxc cxc;

	int num_st_ports;
	struct hfc_st_port st_ports[8];
	struct hfc_st_port *st_port_selected;

	struct hfc_pcm_port pcm_port;
	struct hfc_sys_port sys_port;

	struct hfc_led leds[4];
	u8 gpio_out;
	u8 gpio_en;

	unsigned long io_bus_mem;
	void __iomem *io_mem;

	int clock_source;
	int ram_size;
	int bert_mode;
	int output_level;
};

extern void hfc_softreset(struct hfc_card *card);
extern void hfc_initialize_hw(struct hfc_card *card);

extern void hfc_update_pcm_md0(struct hfc_card *card, u8 otherbits);
extern void hfc_update_pcm_md1(struct hfc_card *card);
extern void hfc_update_st_sync(struct hfc_card *card);
extern void hfc_update_bert_wd_md(struct hfc_card *card, u8 otherbits);
extern void hfc_update_r_ctrl(struct hfc_card *card);
extern void hfc_update_r_brg_pcm_cfg(struct hfc_card *card);
extern void hfc_update_r_ram_misc(struct hfc_card *card);

extern int __devinit hfc_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry);
extern void __devexit hfc_card_remove(struct hfc_card *card);

#endif

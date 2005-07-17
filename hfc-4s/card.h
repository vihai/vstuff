/*
 *
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _HFC_CARD_H
#define _HFC_CARD_H

#include <linux/delay.h>

#include <visdn.h>

#include "port.h"
#include "regs.h"
//#include "fifo.h"

#ifdef DEBUG
#define hfc_debug_card(dbglevel, card, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			format, card->id, ## arg)
#else
#define hfc_debug_card(dbglevel, card, format, arg...) do {} while (0)
#endif

#define hfc_msg_card(level, card, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		format, card->id, ## arg)

struct hfc_card {
	int id;
	spinlock_t lock;

	// This struct contains a copy of some registers whose bits may be
	// changed independently.
	struct
	{
		u8 ram_misc;
		u8 ctrl;
		u8 fifo_md;
		u8 irqmsk_misc;
		u8 bert_wd_md;
	} regs;

	struct pci_dev *pcidev;

	struct proc_dir_entry *proc_dir;
	char proc_dir_name[32];

	struct proc_dir_entry *proc_info;
	struct proc_dir_entry *proc_fifos;

	int num_ports;
	struct hfc_port ports[8];

	int num_fifos;
	struct hfc_fifo fifos[32][2];

	unsigned long io_bus_mem;
	void *io_mem;

	int sync_loss_reported;
	int late_irqs;

	int ignore_first_timer_interrupt;

	int open_ports;

	int clock_source;
	int ramsize;
	int bert_mode;
	int output_level;
};

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
 return readb(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
 writeb(value, card->io_mem + offset);
}

static inline u16 hfc_inw(struct hfc_card *card, int offset)
{
 return readw(card->io_mem + offset);
}

static inline void hfc_outw(struct hfc_card *card, int offset, u16 value)
{
 writew(value, card->io_mem + offset);
}

static inline u32 hfc_inl(struct hfc_card *card, int offset)
{
 return readl(card->io_mem + offset);
}

static inline void hfc_outl(struct hfc_card *card, int offset, u32 value)
{
 writel(value, card->io_mem + offset);
}

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<1000; i++) {
		if (!(hfc_inb(card, hfc_R_STATUS) & hfc_R_STATUS_V_BUSY))
			return;

		udelay(1);
	}

	printk(KERN_ERR hfc_DRIVER_PREFIX
		"card %d: "
		"card is stuck in busy state...\n",
		card->id);
}

#endif

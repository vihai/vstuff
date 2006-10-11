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

#ifndef _HFC_CARD_H
#define _HFC_CARD_H

#include <linux/delay.h>
#include <linux/pci.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>

#include "module.h"
#include "st_port.h"
#include "pcm_port.h"
#include "sys_port.h"
#include "switch.h"
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
	u8 double_clock;
	u8 quartz_49;
	u8 ram_size;
	u8 pwm0;
	u8 pwm1;
	u8 clk_dly_nt;
	u8 clk_dly_te;
	u8 sampl_comp_nt;
	u8 sampl_comp_te;
};

struct hfc_card
{
	struct kref kref;

	spinlock_t lock;

	/* This struct contains a copy of some registers whose bits may be
	 * changed independently.
	 */
	struct
	{
		u8 irqmsk_misc;
	} regs;

	struct pci_dev *pci_dev;
	struct hfc_card_config *config;

	int double_clock;
	int quartz_49;

	struct hfc_switch hfcswitch;

	int num_st_ports;
	struct hfc_st_port *st_ports[8];
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
	int pwm0;
	int pwm1;
};

struct hfc_card *hfc_card_get(struct hfc_card *card);
void hfc_card_put(struct hfc_card *card);

struct hfc_card *hfc_card_alloc(void);
void hfc_card_init(
	struct hfc_card *card,
	struct pci_dev *pci_dev,
	struct hfc_card_config *card_config);
int hfc_card_register(struct hfc_card *card);
void hfc_card_unregister(struct hfc_card *card);
int hfc_card_probe(struct hfc_card *card);
void hfc_card_remove(struct hfc_card *card);

void hfc_card_softreset(struct hfc_card *card);
void hfc_card_initialize_hw(struct hfc_card *card);

void hfc_card_update_pcm_md0(struct hfc_card *card, u8 otherbits);
void hfc_card_update_pcm_md1(struct hfc_card *card);
void hfc_card_update_st_sync(struct hfc_card *card);
void hfc_card_update_bert_wd_md(struct hfc_card *card, u8 otherbits);
void hfc_card_update_r_ctrl(struct hfc_card *card);
void hfc_card_update_r_brg_pcm_cfg(struct hfc_card *card);
void hfc_card_update_r_ram_misc(struct hfc_card *card);



static inline void hfc_card_lock(struct hfc_card *card)
{
	spin_lock_bh(&card->lock);
}

static inline int hfc_card_trylock(struct hfc_card *card)
{
	return spin_trylock(&card->lock);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
	spin_unlock_bh(&card->lock);
}

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
	return ioread8(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
	iowrite8(value, card->io_mem + offset);
}

static inline u16 hfc_inw(struct hfc_card *card, int offset)
{
	return ioread16(card->io_mem + offset);
}

static inline void hfc_outw(struct hfc_card *card, int offset, u16 value)
{
	iowrite16(value, card->io_mem + offset);
}

static inline u32 hfc_inl(struct hfc_card *card, int offset)
{
	return ioread32(card->io_mem + offset);
}

static inline void hfc_outl(struct hfc_card *card, int offset, u32 value)
{
	iowrite32(value, card->io_mem + offset);
}

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<5000; i++) {
		if (!(hfc_inb(card, hfc_R_STATUS) & hfc_R_STATUS_V_BUSY))
			return;

		cpu_relax();
	}

	hfc_msg_card(card, KERN_ERR, hfc_DRIVER_PREFIX
		"Stuck in busy state...\n");
}

#endif

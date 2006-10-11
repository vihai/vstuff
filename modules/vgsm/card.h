/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */



#ifndef _VGSM_CARD_H
#define _VGSM_CARD_H

#include <linux/spinlock.h>
#include <linux/kfifo.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "module.h"
#include "micro.h"

#define vgsm_msg_card(card, level, format, arg...)	\
	printk(level vgsm_DRIVER_PREFIX			\
		"%s-%s "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)


#define vgsm_PCI_MEM_SIZE		0xFF

#define vgsm_DMA_SAMPLES 0x2000

/* Resettng delay */
#define vgsm_RESET_DELAY		20

#ifndef PCI_DMA_32BIT
#define PCI_DMA_32BIT 0x00000000ffffffffULL
#endif

#define vgsm_SERIAL_BUFF	0x1000

enum vgsm_card_flags
{
	VGSM_CARD_FLAGS_SHUTTING_DOWN,
	VGSM_CARD_FLAGS_FW_UPGRADE,
};

struct vgsm_card
{
	struct list_head cards_list_node;
	struct kref kref;

	spinlock_t lock;

	struct pci_dev *pci_dev;

	int id;

	unsigned long flags;

	unsigned long io_bus_mem;
	void *io_mem;
	
	/* DMA bus address */
	void *readdma_mem;
	dma_addr_t readdma_bus_mem;
	int readdma_size;

	dma_addr_t writedma_bus_mem;
	void *writedma_mem;
	int writedma_size;

	int num_micros;
	struct vgsm_micro micros[2];

	int num_modules;
	struct vgsm_module *modules[4];

	struct {
		u8 mask0;
		u8 codec_loop;
	} regs;

	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;
	int rr_last_module;

	struct timer_list maint_timer;
};

struct vgsm_card *vgsm_card_get(struct vgsm_card *card);
void vgsm_card_put(struct vgsm_card *card);

int vgsm_card_probe(
	struct pci_dev *pci_dev, 
	const struct pci_device_id *ent);

void vgsm_card_remove(struct vgsm_card *card);

void vgsm_update_mask0(struct vgsm_card *card);

void vgsm_codec_reset(struct vgsm_card *card);
void vgsm_update_codec(struct vgsm_module *module);

void vgsm_write_msg(
	struct vgsm_card *card, struct vgsm_micro_message *msg);

#endif

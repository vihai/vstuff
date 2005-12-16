/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
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

#include "module.h"

#define vgsm_msg_card(card, level, format, arg...)	\
	printk(level vgsm_DRIVER_PREFIX			\
		"%s-%s "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)


#define vgsm_PCI_MEM_SIZE		0xFF

/* One way DMA area for single module */
#define vgsm_DMA_SINGLE_CHUNK		0x800

/* Resettng delay */
#define vgsm_RESET_DELAY		20

#ifndef PCI_DMA_32BIT
#define PCI_DMA_32BIT 0x00000000ffffffffULL
#endif

#define vgsm_SERIAL_BUFF	0x400

struct vgsm_card
{
	struct list_head cards_list_node;

	spinlock_t lock;

	struct pci_dev *pci_dev;

	int id;

	/* Serial ports */
	u8 ios_12_status;

	unsigned long io_bus_mem;
	void *io_mem;
	
	/* DMA bus address */
	dma_addr_t readdma_bus_mem;
	dma_addr_t writedma_bus_mem;
	
	/* DMA mem address */
	void *readdma_mem;
	void *writedma_mem;

	struct vgsm_module modules[4];
	int num_modules;

	struct {
		u8 mask0;
		u8 codec_loop;
	} regs;
};

struct vgsm_micro_message
{
	union {
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 cmd:2;	/* Command */
	u8 cmd_dep:3;	/* cmd dependent */
	u8 numbytes:3;	/* data length */
	
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 numbytes:3;	/* data length */
	u8 cmd_dep:3;	/* cmd dependent */
	u8 cmd:2;	/* Command */
#endif
	u8 payload[7];
	};

	u8 raw[8];
	};

} __attribute__ ((__packed__));

int vgsm_card_probe(
	struct pci_dev *pci_dev, 
	const struct pci_device_id *ent);

void vgsm_card_remove(struct vgsm_card *card);

void vgsm_send_msg(
	struct vgsm_card *card,
	int micro,
	struct vgsm_micro_message *msg);

void vgsm_update_mask0(struct vgsm_card *card);
void vgsm_update_codec(struct vgsm_module *module);

#endif

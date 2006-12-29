/*
 * VoiSmart vDSP board driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
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

#define vdsp_msg_card(card, level, format, arg...)	\
	printk(level vdsp_DRIVER_PREFIX			\
		"%s-%s "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)


#define vdsp_PCI_MEM_SIZE		0x00010000

enum vdsp_card_flags
{
	VGSM_CARD_FLAGS_SHUTTING_DOWN,
};

struct vdsp_card
{
	struct list_head cards_list_node;
	struct kref kref;

	spinlock_t lock;

	struct pci_dev *pci_dev;

	int id;

	unsigned long flags;

	unsigned long regs_bus_ptr;
	void *regs_ptr;

	unsigned long mem_bus_ptr;
	void *mem_ptr;
};

struct vdsp_card *vdsp_card_get(struct vdsp_card *card);
void vdsp_card_put(struct vdsp_card *card);

int vdsp_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent);

void vdsp_card_remove(struct vdsp_card *card);

#endif

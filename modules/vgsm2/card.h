/*
 * VoiSmart vGSM-II board driver
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

#include "module.h"

#define vgsm_msg_card(card, level, format, arg...)	\
	printk(level vgsm_DRIVER_PREFIX			\
		"%s-%s "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)


#define vgsm_PCI_MEM_SIZE		0x00010000

enum vgsm_card_flags
{
	VGSM_CARD_FLAGS_SHUTTING_DOWN,
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

	int num_modules;
	struct vgsm_module *modules[4];

	struct {
		u8 mask0;
	} regs;
};

struct vgsm_card *vgsm_card_get(struct vgsm_card *card);
void vgsm_card_put(struct vgsm_card *card);

int vgsm_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent);

void vgsm_card_remove(struct vgsm_card *card);

#endif

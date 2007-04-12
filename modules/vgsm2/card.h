/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
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
#include "sim.h"

#ifdef DEBUG_CODE
#define vgsm_debug_card(card, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG vgsm_DRIVER_PREFIX			\
			"%s-%s: "					\
			format,						\
			(card)->pci_dev->dev.bus->name,		\
			(card)->pci_dev->dev.bus_id,		\
			## arg)

#else
#define vgsm_debug_card(card, dbglevel, format, arg...) do {} while (0)
#endif

#define vgsm_msg_card(card, level, format, arg...)	\
	printk(level vgsm_DRIVER_PREFIX			\
		"%s:%s: "				\
		format,					\
		(card)->pci_dev->dev.bus->name,		\
		(card)->pci_dev->dev.bus_id,		\
		## arg)

enum vgsm_card_flags
{
	VGSM_CARD_FLAGS_SHUTTING_DOWN,
	VGSM_CARD_FLAGS_RECONFIG_PENDING,
};

struct vgsm_card
{
	struct list_head cards_list_node;
	struct kref kref;

	spinlock_t lock;

	struct pci_dev *pci_dev;

	int id;

	unsigned long flags;

	unsigned long regs_bus_mem;
	void *regs_mem;

	unsigned long fifo_bus_mem;
	void *fifo_mem;

	u8 mes_number;
	u8 sims_number;

	struct vgsm_module *modules[4];
	struct vgsm_sim sims[4];

	u32 serial_number;
};

void vgsm_card_update_router(struct vgsm_card *card);

struct vgsm_card *vgsm_card_get(struct vgsm_card *card);
void vgsm_card_put(struct vgsm_card *card);

struct vgsm_card *vgsm_card_create(
	struct vgsm_card *card,
	struct pci_dev *pci_dev,
	int id);
void vgsm_card_destroy(struct vgsm_card *card);

int vgsm_card_ioctl_fw_version(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg);
int vgsm_card_ioctl_fw_upgrade(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg);
int vgsm_card_ioctl_fw_read(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg);

int vgsm_card_register(struct vgsm_card *card);
void vgsm_card_unregister(struct vgsm_card *card);

int vgsm_card_probe(struct vgsm_card *card);
void vgsm_card_remove(struct vgsm_card *card);

int __init vgsm_card_modinit(void);
void __exit vgsm_card_modexit(void);

#endif

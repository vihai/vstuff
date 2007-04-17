/*
 * VoiSmart vDSP board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/version.h>

#include "vdsp.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"

/* HW initialization */

static int vdsp_initialize_hw(struct vdsp_card *card)
{

	vdsp_msg(KERN_DEBUG, "VGSM card initialized\n");

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t vdsp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t vdsp_interrupt(int irq, void *dev_id)
#endif
{
	struct vdsp_card *card = dev_id;

	if (unlikely(!card)) {
		vdsp_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

//	if (!myirq)
		return IRQ_NONE;


	return IRQ_HANDLED;
}

static void vdsp_card_release(struct kref *kref)
{
	struct vdsp_card *card = container_of(kref, struct vdsp_card, kref);

	kfree(card);
}

struct vdsp_card *vdsp_card_get(struct vdsp_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void vdsp_card_put(struct vdsp_card *card)
{
	kref_put(&card->kref, vdsp_card_release);
}

static void vdsp_card_init(
	struct vdsp_card *card,
	struct pci_dev *pci_dev)
{
	memset(card, 0, sizeof(*card));

	card->pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	spin_lock_init(&card->lock);
}

int vdsp_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	struct vdsp_card *card;
	int err;

	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto err_card_alloc;
	}

	vdsp_card_init(card, pci_dev);

	/* From here on vdsp_msg_card may be used */
	err = pci_enable_device(pci_dev);
	if (err < 0)
		goto err_pci_enable_device;

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(pci_dev, vdsp_DRIVER_NAME);
	if(err < 0) {
		vdsp_msg_card(card, KERN_CRIT,
			     "cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		vdsp_msg_card(card, KERN_CRIT,
			     "No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->mem_bus_ptr = pci_resource_start(pci_dev, 1);
	if (!card->mem_bus_ptr) {
		vdsp_msg_card(card, KERN_CRIT,
			     "No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_mem_base;
	}

	card->mem_ptr = ioremap(card->mem_bus_ptr, 0x400000);
	if(!card->mem_ptr) {
		vdsp_msg_card(card, KERN_CRIT,
			     "Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap_mem;
	}

	card->regs_bus_ptr = pci_resource_start(pci_dev, 2);
	if (!card->regs_bus_ptr) {
		vdsp_msg_card(card, KERN_CRIT,
			     "No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_regs_region;
	}

	card->regs_ptr = ioremap(card->regs_bus_ptr, 0x800000);
	if(!card->regs_ptr) {
		vdsp_msg_card(card, KERN_CRIT,
			     "Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap_regs;
	}

	vdsp_msg(KERN_DEBUG, "vDSP card found at 0x%08lx\n",
		card->regs_bus_ptr);

	/* Requesting IRQ */
	err = request_irq(card->pci_dev->irq, &vdsp_interrupt,
			  SA_SHIRQ, vdsp_DRIVER_NAME, card);
	if (err < 0) {
		vdsp_msg_card(card, KERN_CRIT,
			     "unable to register irq\n");
		goto err_request_irq;
	}

	vdsp_initialize_hw(card);

	return 0;

	free_irq(pci_dev->irq, card);
err_request_irq:
	iounmap(card->regs_ptr);
err_ioremap_regs:
err_no_regs_region:
	iounmap(card->mem_ptr);
err_ioremap_mem:
err_no_mem_base:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
err_pci_enable_device:
	kfree(card);
err_card_alloc:

	return err;
}

void vdsp_card_remove(struct vdsp_card *card)
{
	/* Clean up any allocated resources and stuff here */

	vdsp_msg_card(card, KERN_INFO,
		"shutting down card at %#08lx.\n", card->regs_bus_ptr);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

	/* Disable IRQs */

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	/* Free IRQ */
	free_irq(card->pci_dev->irq, card);

	iounmap(card->regs_ptr);
	iounmap(card->mem_ptr);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);

	kfree(card);
}

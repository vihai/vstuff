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

#include <linux/config.h>
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

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"

/* HW initialization */

static int vgsm_initialize_hw(struct vgsm_card *card)
{

	vgsm_msg(KERN_DEBUG, "VGSM card initialized\n");

	return 0;
}

static irqreturn_t vgsm_interrupt(int irq,
	void *dev_id,
	struct pt_regs *regs)
{
	struct vgsm_card *card = dev_id;

	if (unlikely(!card)) {
		vgsm_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

//	if (!myirq)
		return IRQ_NONE;


	return IRQ_HANDLED;
}

static void vgsm_card_release(struct kref *kref)
{
	struct vgsm_card *card = container_of(kref, struct vgsm_card, kref);

	kfree(card);
}

struct vgsm_card *vgsm_card_get(struct vgsm_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void vgsm_card_put(struct vgsm_card *card)
{
	kref_put(&card->kref, vgsm_card_release);
}

static void vgsm_card_init(
	struct vgsm_card *card,
	struct pci_dev *pci_dev,
	int id)
{
	memset(card, 0, sizeof(*card));

	card->pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	card->id = id;

	spin_lock_init(&card->lock);
}

int vgsm_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	struct vgsm_card *card;
	static int numcards;
	int err;
	int i;

	const struct {
		u16 rx_base;
		u16 rx_size;
		u16 tx_base;
		u16 tx_size;
	} fifo_config[] = {
		{ VGSM_FIFO_RX_0_BASE, VGSM_FIFO_RX_0_SIZE,
		  VGSM_FIFO_TX_0_BASE, VGSM_FIFO_TX_0_SIZE },
		{ VGSM_FIFO_RX_1_BASE, VGSM_FIFO_RX_1_SIZE,
		  VGSM_FIFO_TX_1_BASE, VGSM_FIFO_TX_1_SIZE },
		{ VGSM_FIFO_RX_2_BASE, VGSM_FIFO_RX_2_SIZE,
		  VGSM_FIFO_TX_2_BASE, VGSM_FIFO_TX_2_SIZE },
		{ VGSM_FIFO_RX_3_BASE, VGSM_FIFO_RX_3_SIZE,
		  VGSM_FIFO_TX_3_BASE, VGSM_FIFO_TX_3_SIZE }
	};

	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto err_card_alloc;
	}

	vgsm_card_init(card, pci_dev, numcards++);

	card->num_modules = 4; /* DO MODULE PROBING! FIXME TODO */

	for (i=0; i<card->num_modules; i++) {
		char name[8];

		snprintf(name, sizeof(name), "gsm%d", i);

		card->modules[i] = vgsm_module_alloc(card, i, name,
					fifo_config[i].rx_base,
					fifo_config[i].rx_size,
					fifo_config[i].tx_base,
					fifo_config[i].tx_size);
		if (!card->modules[i]) {
			err = -ENOMEM;
			goto err_modules_alloc;
		}
	}

	/* From here on vgsm_msg_card may be used */

	err = pci_enable_device(pci_dev);
	if (err < 0)
		goto err_pci_enable_device;

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(pci_dev, vgsm_DRIVER_NAME);
	if(err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev, 1);
	if (!card->io_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->io_mem = ioremap(card->io_bus_mem, vgsm_PCI_MEM_SIZE);

	if(!card->io_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	vgsm_msg(KERN_DEBUG, "vGSM-II card found at 0x%08lx mapped at %p\n",
		card->io_bus_mem, card->io_mem);

	/* Requesting IRQ */
	err = request_irq(card->pci_dev->irq, &vgsm_interrupt,
			  SA_SHIRQ, vgsm_DRIVER_NAME, card);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "unable to register irq\n");
		goto err_request_irq;
	}

	vgsm_initialize_hw(card);

	/* Enable interrupts */
//	card->regs.mask0 = 0;
//	vgsm_outb(card, VGSM_MASK0, card->regs.mask0);

	for (i=0; i<card->num_modules; i++) {
		err = vgsm_module_register(card->modules[i]);
		if (err < 0)
			goto err_module_register;
	}

	spin_lock(&vgsm_cards_list_lock);
	list_add_tail(&card->cards_list_node, &vgsm_cards_list);
	spin_unlock(&vgsm_cards_list_lock);

	return 0;

//	free_irq(pci_dev->irq, card);
err_request_irq:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
err_pci_enable_device:
err_module_register:
	for(i=card->num_modules; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_unregister(card->modules[i]);
	}
err_modules_alloc:
	for(i=card->num_modules; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_put(card->modules[i]);
	}
	kfree(card);
err_card_alloc:

	return err;
}

void vgsm_card_remove(struct vgsm_card *card)
{
	int i;
	int shutting_down = FALSE;

	/* Clean up any allocated resources and stuff here */

	vgsm_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n", card->io_mem);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

	spin_lock(&vgsm_cards_list_lock);
	list_del(&card->cards_list_node);
	spin_unlock(&vgsm_cards_list_lock);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_unregister(card->modules[i]);

#if 0
		vgsm_card_lock(card);
		vgsm_module_send_power_get(card->modules[i]);
		vgsm_card_unlock(card);

		wait_for_completion_timeout(
			&card->modules[i].read_status_completion, 2 * HZ);

		if (test_bit(VGSM_MODULE_STATUS_ON,
						&card->modules[i].status)) {

			/* Force an emergency shutdown if the application did
			 * not do its duty
			 */

			vgsm_card_lock(card);
			vgsm_module_send_onoff(&card->modules[i],
				VGSM_CMD_MAINT_ONOFF_EMERG_OFF);
			vgsm_card_unlock(card);

			shutting_down = TRUE;

			vgsm_msg_card(card, KERN_NOTICE,
				"Module %d has not been shut down, forcing"
				" emergency shutdown\n",
				card->modules[i].id);
		}
#endif
	}

	if (shutting_down) {
#if 0

		msleep(3200);

		for(i=0; i<card->num_modules; i++) {
			vgsm_card_lock(card);
			vgsm_module_send_onoff(&card->modules[i], 0);
			vgsm_card_unlock(card);
		}
#endif
	}

	/* Disable IRQs */
	/* Reset FPGA  */

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	/* Free IRQ */
	free_irq(card->pci_dev->irq, card);

	/* Unmap */
	iounmap(card->io_mem);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);

	for(i=card->num_modules; i>=0; i--)
		vgsm_module_put(card->modules[i]);

	kfree(card);
}

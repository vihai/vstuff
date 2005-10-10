/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include "card.h"
#include "fifo.h"
#include "card_inline.h"
#include "card_sysfs.h"
#include "chan_sysfs.h"
#include "st_port.h"
#include "st_port_sysfs.h"
#include "pcm_port_sysfs.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id hfc_pci_ids[] = {
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_2BD0,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B006,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B007,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B008,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B009,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00C,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ABOCOM, PCI_DEVICE_ID_ABOCOM_2BD1,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASUSTEK, PCI_DEVICE_ID_ASUSTEK_0675,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_T_CONCEPT,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_A1T,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ANIGMA, PCI_DEVICE_ID_ANIGMA_MC145575,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ZOLTRIX, PCI_DEVICE_ID_ZOLTRIX_2BD0,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_E,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_E,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hfc_pci_ids);

static int __devinit hfc_probe(struct pci_dev *dev
			, const struct pci_device_id *ent);
static void __devexit hfc_remove(struct pci_dev *dev);

struct pci_driver hfc_driver = {
	.name     = hfc_DRIVER_NAME,
	.id_table = hfc_pci_ids,
	.probe    = hfc_probe,
	.remove   = hfc_remove,
};

/******************************************
 * HW routines
 ******************************************/

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<100; i++) {
		if (!(hfc_inb(card, hfc_STATUS) & hfc_STATUS_BUSY))
			return;

		udelay(5);
	}

	hfc_msg_card(card, KERN_ERR, hfc_DRIVER_PREFIX
		"Stuck in busy state...\n");
}

void hfc_softreset(struct hfc_card *card)
{
	hfc_msg_card(card, KERN_INFO,
		"resetting\n");

	hfc_outb(card, hfc_CIRM, hfc_CIRM_RESET);	// softreset on
	mb();
	udelay(6);	  // wait at least 5.21us
	hfc_outb(card, hfc_CIRM, 0);	// softreset off
	mb();

	hfc_wait_busy(card);
}

void hfc_initialize_hw(struct hfc_card *card)
{
	card->regs.m1 = 0;
	hfc_outb(card, hfc_INT_M1, card->regs.m1);

	card->regs.m2 = 0;
	hfc_outb(card, hfc_INT_M2, card->regs.m2);

	card->regs.trm = 0;
	hfc_outb(card, hfc_TRM, card->regs.trm);

	// S/T Auto awake
	card->regs.sctrl_e = hfc_SCTRL_E_AUTO_AWAKE;
	hfc_outb(card, hfc_SCTRL_E, card->regs.sctrl_e);

	// HFC Master Mode
	hfc_outb(card, hfc_MST_MODE, hfc_MST_MODE_MASTER);

	// All bchans are HDLC by default, not useful, actually
	// since mode is set during open()
	card->regs.ctmt = 0;
	hfc_outb(card, hfc_CTMT, card->regs.ctmt);

	// bit order
	card->regs.cirm = 0;
	hfc_outb(card, hfc_CIRM, card->regs.cirm);

	// Enable D-rx FIFO. At least one FIFO must be enabled (by specs)
	card->regs.fifo_en = hfc_FIFO_EN_DRX;
	hfc_outb(card, hfc_FIFO_EN, card->regs.fifo_en);

	card->late_irqs=0;

	// Clear already pending ints
	hfc_inb(card, hfc_INT_S1);
	hfc_inb(card, hfc_INT_S2);

	hfc_st_port_update_sctrl(&card->st_port);
	hfc_st_port_update_sctrl_r(&card->st_port);
	hfc_st_port_update_st_clk_dly(&card->st_port);

	// Enable IRQ output
	card->regs.m1 = hfc_INT_M1_DREC | hfc_INT_M1_L1STATE | hfc_INT_M1_TIMER;
	hfc_outb(card, hfc_INT_M1, card->regs.m1);

	card->regs.m2 = hfc_INT_M2_IRQ_ENABLE;
	hfc_outb(card, hfc_INT_M2, card->regs.m2);
}

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_fifo *fifo)
{
	if (fifo->connected_chan)
		schedule_work(&fifo->work);
}

static inline void hfc_handle_fifo_tx_interrupt(struct hfc_fifo *fifo)
{
	if (fifo->connected_chan)
		visdn_pass_wake_queue(&fifo->connected_chan->chan->visdn_chan);
}

static inline void hfc_handle_timer_interrupt(struct hfc_card *card)
{
}

static inline void hfc_handle_state_interrupt(struct hfc_st_port *port)
{
	schedule_work(&port->state_change_work);
}

static irqreturn_t hfc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct hfc_card *card = dev_id;

	if (unlikely(!card)) {
		hfc_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

	u8 status = hfc_inb(card, hfc_STATUS);
	if (!(status & hfc_STATUS_ANYINT)) {
		// maybe we are sharing the irq
		return IRQ_NONE;
	}

	/* We used to ingore the IRQ when the card was in processing
	 * state but apparently there is no restriction to access the
	 * card in such state:
	 *
	 * Joerg Ciesielski wrote:
	 * > There is no restriction for the IRQ handler to access
	 * > HFC-S PCI during processing phase. A IRQ latency of 375 us
	 * > is also no problem since there are no interrupt sources in
	 * > HFC-S PCI which must be handled very fast.
	 * > Due to its deep fifos the IRQ latency can be several ms with
	 * > out the risk of loosing data. Even the S/T state interrupts
	 * > must not be handled with a latency less than <5ms.
	 * >
	 * > The processing phase only indicates that HFC-S PCI is
	 * > processing the Fifos as PCI master so that data is read and
	 * > written in the 32k memory window. But there is no restriction
	 * > to access data in the memory window during this time.
	 */

	u8 s1 = hfc_inb(card, hfc_INT_S1);
	u8 s2 = hfc_inb(card, hfc_INT_S2);

	if (s1 != 0) {
		if (s1 & hfc_INT_S1_TIMER) {
			// timer (bit 7)
			hfc_handle_timer_interrupt(card);
		}

		if (s1 & hfc_INT_S1_L1STATE) {
			// state machine (bit 6)
			hfc_handle_state_interrupt(&card->st_port);
		}

		// D chan RX (bit 5)
		if (s1 & hfc_INT_S1_DREC)
			hfc_handle_fifo_rx_interrupt(&card->fifos[D][RX]);

		// B1 chan RX (bit 3)
		if (s1 & hfc_INT_S1_B1REC)
			hfc_handle_fifo_rx_interrupt(&card->fifos[B1][RX]);

		// B2 chan RX (bit 4)
		if (s1 & hfc_INT_S1_B2REC)
			hfc_handle_fifo_rx_interrupt(&card->fifos[B2][RX]);

		// D chan TX (bit 2)
		if (s1 & hfc_INT_S1_DTRANS)
			hfc_handle_fifo_tx_interrupt(&card->fifos[D][TX]);

		// B1 chan TX (bit 0)
		if (s1 & hfc_INT_S1_B1TRANS)
			hfc_handle_fifo_tx_interrupt(&card->fifos[B1][TX]);

		// B2 chan TX (bit 1)
		if (s1 & hfc_INT_S1_B2TRANS)
			hfc_handle_fifo_tx_interrupt(&card->fifos[B2][TX]);
	}

	if (s2 != 0) {
		if (s2 & hfc_INT_S2_PCISTUCK) {
			// kaboom irq (bit 7)

			/* CologneChip says:
			 *
			 * the meaning of this fatal error bit is that HFC-S PCI as PCI
			 * master could not access the PCI bus within 125us to finish its
			 * data processing. If this happens only very seldom it does not
			 * cause big problems but of course some B-channel or D-channel
			 * data will be corrupted due to this event.
			 *
			 * Unfortunately this bit is only set once after the problem occurs
			 * and can only be reseted by a software reset. That means it is not
			 * easily possible to check how often this fatal error happens.
			 */

			if(!card->sync_loss_reported) {
				hfc_msg_card(card, KERN_CRIT,
					"sync lost, pci performance too low!\n");

				card->sync_loss_reported = TRUE;
			}
		}

		if (s2 & hfc_INT_S2_GCI_MON_REC) {
			// RxR monitor channel (bit 2)
		}

		if (s2 & hfc_INT_S2_GCI_I_CHG) {
			// GCI I-change  (bit 1)
		}

		if (s2 & hfc_INT_S2_PROC_TRANS){
			// processing/non-processing transition  (bit 0)
		}

	}

	return IRQ_HANDLED;
}

/******************************************
 * Module initialization and cleanup
 ******************************************/

static int __devinit hfc_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	int err;

	struct hfc_card *card;
	card = kmalloc(sizeof(struct hfc_card), GFP_KERNEL);
	if (!card) {
		hfc_msg(KERN_CRIT, "unable to kmalloc!\n");
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	memset(card, 0x00, sizeof(*card));

	spin_lock_init(&card->lock);

	card->pcidev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	// From here on hfc_msg_card may be used

	err = pci_enable_device(pci_dev);
	if (err < 0) {
		goto err_pci_enable_device;
	}

	err = pci_set_dma_mask(pci_dev, PCI_DMA_32BIT);
	if (err < 0) {
		hfc_msg_card(card, KERN_ERR,
			"No suitable DMA configuration available.\n");
		goto err_pci_set_dma_mask;
	}

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(pci_dev, hfc_DRIVER_NAME);
	if(err < 0) {
		hfc_msg_card(card, KERN_CRIT,
			"cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		hfc_msg_card(card, KERN_CRIT,
			"No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev,1);
	if (!card->io_bus_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	card->io_mem = ioremap(card->io_bus_mem, hfc_PCI_MEM_SIZE);
	if(!card->io_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	// pci_alloc_consistent guarantees alignment (Documentation/DMA-mapping.txt)
	card->fifo_mem = pci_alloc_consistent(pci_dev, hfc_FIFO_SIZE,
						&card->fifo_bus_mem);
	if (!card->fifo_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"unable to allocate FIFO DMA memory!\n");
		err = -ENOMEM;
		goto err_alloc_fifo;
	}

	memset(card->fifo_mem, 0x00, hfc_FIFO_SIZE);

	pci_write_config_dword(card->pcidev, hfc_PCI_MWBA, card->fifo_bus_mem);

	err = request_irq(card->pcidev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card);
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT,
			"unable to register irq\n");
		goto err_request_irq;
	}

	struct hfc_fifo *fifo;

	fifo = &card->fifos[0][RX];
	hfc_fifo_init(&card->fifos[0][RX], card, hfc_FIFO_D, RX);
	fifo->fifo_base = card->fifo_mem + 0x4000;
	fifo->z_base    = card->fifo_mem + 0x4000;
	fifo->z1_base   = card->fifo_mem + 0x6080;
	fifo->z2_base   = card->fifo_mem + 0x6082;
	fifo->z_min     = 0x0000;
	fifo->z_max     = 0x01FF;
	fifo->f_min     = 0x10;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x60a0;
	fifo->f2        = card->fifo_mem + 0x60a1;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	fifo = &card->fifos[0][TX];
	hfc_fifo_init(&card->fifos[0][TX], card, hfc_FIFO_D, TX);
	fifo->fifo_base = card->fifo_mem + 0x0000;
	fifo->z_base    = card->fifo_mem + 0x0000;
	fifo->z1_base   = card->fifo_mem + 0x2080;
	fifo->z2_base   = card->fifo_mem + 0x2082;
	fifo->z_min     = 0x0000;
	fifo->z_max     = 0x01FF;
	fifo->f_min     = 0x10;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x20a0;
	fifo->f2        = card->fifo_mem + 0x20a1;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	fifo = &card->fifos[1][RX];
	hfc_fifo_init(&card->fifos[1][RX], card, hfc_FIFO_B1, RX);
	fifo->fifo_base = card->fifo_mem + 0x4200;
	fifo->z_base    = card->fifo_mem + 0x4000;
	fifo->z1_base   = card->fifo_mem + 0x6000;
	fifo->z2_base   = card->fifo_mem + 0x6002;
	fifo->z_min     = 0x0200;
	fifo->z_max     = 0x1FFF;
	fifo->f_min     = 0x00;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x6080;
	fifo->f2        = card->fifo_mem + 0x6081;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	fifo = &card->fifos[1][TX];
	hfc_fifo_init(&card->fifos[1][TX], card, hfc_FIFO_B1, TX);
	fifo->fifo_base = card->fifo_mem + 0x0200;
	fifo->z_base    = card->fifo_mem + 0x0000;
	fifo->z1_base   = card->fifo_mem + 0x2000;
	fifo->z2_base   = card->fifo_mem + 0x2002;
	fifo->z_min     = 0x0200;
	fifo->z_max     = 0x1FFF;
	fifo->f_min     = 0x00;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x2080;
	fifo->f2        = card->fifo_mem + 0x2081;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	fifo = &card->fifos[2][RX];
	hfc_fifo_init(&card->fifos[2][RX], card, hfc_FIFO_B2, RX);
	fifo->fifo_base = card->fifo_mem + 0x6200,
	fifo->z_base    = card->fifo_mem + 0x6000;
	fifo->z1_base   = card->fifo_mem + 0x6100;
	fifo->z2_base   = card->fifo_mem + 0x6102;
	fifo->z_min     = 0x0200;
	fifo->z_max     = 0x1FFF;
	fifo->f_min     = 0x00;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x6180;
	fifo->f2        = card->fifo_mem + 0x6181;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	fifo = &card->fifos[2][TX];
	hfc_fifo_init(&card->fifos[2][TX], card, hfc_FIFO_B2, TX);
	fifo->fifo_base = card->fifo_mem + 0x2200;
	fifo->z_base    = card->fifo_mem + 0x2000;
	fifo->z1_base   = card->fifo_mem + 0x2100;
	fifo->z2_base   = card->fifo_mem + 0x2102;
	fifo->z_min     = 0x0200;
	fifo->z_max     = 0x1FFF;
	fifo->f_min     = 0x00;
	fifo->f_max     = 0x1F;
	fifo->f1        = card->fifo_mem + 0x2180;
	fifo->f2        = card->fifo_mem + 0x2181;
	fifo->fifo_size = fifo->z_max - fifo->z_min + 1;
	fifo->f_num     = fifo->f_max - fifo->f_min + 1;

	hfc_st_port_init(&card->st_port, card, "st0");
	hfc_pcm_port_init(&card->pcm_port, card, "pcm");

	hfc_card_lock(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	// Ok, the hardware is ready and the data structures are initialized,
	// we can now register to the system.

	visdn_port_register(&card->st_port.visdn_port);

	visdn_chan_register(&card->st_port.chans[D].visdn_chan);
	hfc_chan_sysfs_create_files_D(&card->st_port.chans[D]);

	visdn_chan_register(&card->st_port.chans[B1].visdn_chan);
	hfc_chan_sysfs_create_files_B(&card->st_port.chans[B1]);

	visdn_chan_register(&card->st_port.chans[B2].visdn_chan);
	hfc_chan_sysfs_create_files_B(&card->st_port.chans[B2]);

	visdn_chan_register(&card->st_port.chans[E].visdn_chan);
	hfc_chan_sysfs_create_files_E(&card->st_port.chans[E]);

	visdn_chan_register(&card->st_port.chans[SQ].visdn_chan);
	hfc_chan_sysfs_create_files_SQ(&card->st_port.chans[SQ]);

	hfc_st_port_sysfs_create_files(&card->st_port);

	visdn_port_register(&card->pcm_port.visdn_port);

	hfc_pcm_port_sysfs_create_files(&card->pcm_port);

// -------------------------------------------------------

	err = hfc_card_sysfs_create_files(card);
	if (err < 0)
		goto err_card_sysfs_create_files;

	hfc_msg_card(card, KERN_INFO,
		"configured at mem %#lx (0x%p) IRQ %u\n",
		card->io_bus_mem,
		card->io_mem,
		card->pcidev->irq);

	return 0;

	visdn_port_unregister(&card->st_port.visdn_port);

	hfc_card_sysfs_delete_files(card);
err_card_sysfs_create_files:
	free_irq(pci_dev->irq, card);
err_request_irq:
	pci_free_consistent(pci_dev, hfc_FIFO_SIZE,
		card->fifo_mem, card->fifo_bus_mem);
err_alloc_fifo:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
err_pci_set_dma_mask:
err_pci_enable_device:
	kfree(card);
err_alloc_hfccard:

	return err;
}

static void __devexit hfc_remove(struct pci_dev *pci_dev)
{
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	hfc_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n",
		card->io_mem);

	hfc_card_sysfs_delete_files(card);

	visdn_port_unregister(&card->pcm_port.visdn_port);

	visdn_chan_unregister(&card->st_port.chans[SQ].visdn_chan);
	visdn_chan_unregister(&card->st_port.chans[E].visdn_chan);
	visdn_chan_unregister(&card->st_port.chans[B2].visdn_chan);
	visdn_chan_unregister(&card->st_port.chans[B1].visdn_chan);
	visdn_chan_unregister(&card->st_port.chans[D].visdn_chan);
	visdn_port_unregister(&card->st_port.visdn_port);

	hfc_card_lock(card);
	hfc_softreset(card);
	// disable memio and bustmaster
	hfc_card_unlock(card);

	// There should be no interrupt from here on

	pci_write_config_word(pci_dev, PCI_COMMAND, 0);

	free_irq(pci_dev->irq, card);

	pci_free_consistent(pci_dev, hfc_FIFO_SIZE,
		card->fifo_mem, card->fifo_bus_mem);

	iounmap(card->io_mem);

	pci_release_regions(pci_dev);

	pci_disable_device(pci_dev);

	kfree(card);
}

#ifdef DEBUG_CODE
static ssize_t hfc_show_debug_level(
	struct device_driver *driver,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t hfc_store_debug_level(
	struct device_driver *driver,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	hfc_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

DRIVER_ATTR(debug_level, S_IRUGO | S_IWUSR,
	hfc_show_debug_level,
	hfc_store_debug_level);
#endif

/******************************************
 * Module stuff
 ******************************************/

static int __init hfc_init_module(void)
{
	int err;

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " loading\n");

	err = pci_register_driver(&hfc_driver);
	if (err < 0)
		goto err_pci_register_driver;

#ifdef DEBUG_CODE
	err = driver_create_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif

	return 0;

#ifdef DEBUG_CODE
	driver_remove_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif
err_pci_register_driver:

	return err;
}

module_init(hfc_init_module);

static void __exit hfc_module_exit(void)
{
#ifdef DEBUG_CODE
	driver_remove_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif

	pci_unregister_driver(&hfc_driver);

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " unloaded\n");
}

module_exit(hfc_module_exit);

MODULE_DESCRIPTION(hfc_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

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
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>

#include "card.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "card_inline.h"
#include "st_port.h"

static int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}

static ssize_t hfc_show_fifo_state(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	int i;

	*buf = '\0';

	sanprintf(buf, PAGE_SIZE,
		"       Receive                   Transmit\n"
		"Fifo#  F1 F2   Z1   Z2 Used      F1 F2   Z1   Z2 Used"
		" Connected\n");

	for (i=0; i<ARRAY_SIZE(card->fifos); i++) {
		struct hfc_fifo *fifo_rx = &card->fifos[i][RX];
		struct hfc_fifo *fifo_tx = &card->fifos[i][TX];

		sanprintf(buf, PAGE_SIZE,
			"%2d     %02x %02x %04x %04x %4d %c%c%c"
			"        %02x %02x %04x %04x %4d %c%c%c",
			fifo_rx->id,
			*fifo_rx->f1,
			*fifo_rx->f2,
			Z1_F2(fifo_rx),
			Z2_F2(fifo_rx),
			hfc_fifo_used_rx(fifo_rx),
			fifo_rx->framer_enabled ? 'H' : ' ',
			fifo_rx->enabled ? 'E' : ' ',
			hfc_fifo_is_running(fifo_rx) ? 'R' : ' ',
			*fifo_tx->f1,
			*fifo_tx->f2,
			Z1_F1(fifo_tx),
			Z2_F1(fifo_tx),
			hfc_fifo_used_tx(fifo_tx),
			fifo_tx->framer_enabled ? 'H' : ' ',
			fifo_tx->enabled ? 'E' : ' ',
			hfc_fifo_is_running(fifo_tx) ? 'R' : ' ');

		if (fifo_tx->connected_chan) {
			sanprintf(buf, PAGE_SIZE,
				" st:%s",
				fifo_tx->connected_chan->visdn_chan.name);
		}

		if (fifo_rx->connected_chan) {
			sanprintf(buf, PAGE_SIZE,
				" st:%s",
				fifo_rx->connected_chan->visdn_chan.name);
		}

		sanprintf(buf, PAGE_SIZE, "\n");
	}

	return strlen(buf);
}

static DEVICE_ATTR(fifo_state, S_IRUGO,
		hfc_show_fifo_state,
		NULL);

int hfc_card_sysfs_create_files(
	struct hfc_card *card)
{
	int err;

	err = device_create_file(
		&card->pci_dev->dev,
		&dev_attr_fifo_state);
	if (err < 0)
		goto err_device_create_file_fifo_state;

	return 0;

	device_remove_file(&card->pci_dev->dev, &dev_attr_fifo_state);
err_device_create_file_fifo_state:

	return err;
}

void hfc_card_sysfs_delete_files(
	struct hfc_card *card)
{
	device_remove_file(&card->pci_dev->dev, &dev_attr_fifo_state);
}

/******************************************
 * HW routines
 ******************************************/

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<100; i++) {
		if (!(hfc_inb(card, hfc_STATUS) & hfc_STATUS_BUSY))
			return;

		udelay(100);
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
	udelay(6);			// wait at least 5.21us
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

	/* S/T Auto awake */
	card->regs.sctrl_e = hfc_SCTRL_E_AUTO_AWAKE;
	hfc_outb(card, hfc_SCTRL_E, card->regs.sctrl_e);

	/* HFC Master Mode */
	hfc_outb(card, hfc_MST_MODE, hfc_MST_MODE_MASTER);

	/* All bchans are HDLC by default, not useful, actually
	   since mode is set during open() */
	card->regs.ctmt = 0;
	hfc_outb(card, hfc_CTMT, card->regs.ctmt);

	/* bit order */
	card->regs.cirm = 0;

	/* Enable D-rx FIFO. At least one FIFO must be enabled (by specs) */
	card->regs.fifo_en = hfc_FIFO_EN_DRX;
	hfc_outb(card, hfc_FIFO_EN, card->regs.fifo_en);

	/* Clear already pending ints */
	hfc_inb(card, hfc_INT_S1);
	hfc_inb(card, hfc_INT_S2);

	hfc_st_port_update_sctrl(&card->st_port);
	hfc_st_port_update_sctrl_r(&card->st_port);
	hfc_st_port_update_st_clk_dly(&card->st_port);

	/* Enable IRQ output */
	card->regs.m1 =
		hfc_INT_M1_DREC |
		hfc_INT_M1_B1TRANS |
		hfc_INT_M1_B2TRANS |
		hfc_INT_M1_DTRANS |
		hfc_INT_M1_B1REC |
		hfc_INT_M1_B2REC |
		hfc_INT_M1_DREC |
		hfc_INT_M1_L1STATE |
		hfc_INT_M1_TIMER;
	hfc_outb(card, hfc_INT_M1, card->regs.m1);

	card->regs.m2 = hfc_INT_M2_IRQ_ENABLE;
	hfc_outb(card, hfc_INT_M2, card->regs.m2);
}

void hfc_card_fifo_update(struct hfc_card *card)
{
	int i;

	for (i=0; i<ARRAY_SIZE(card->fifos); i++) {
		hfc_fifo_configure(&card->fifos[i][RX]);
		hfc_fifo_configure(&card->fifos[i][TX]);
	}
}

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_fifo *fifo)
{
	if (fifo->connected_chan)
		schedule_work(&fifo->connected_chan->rx_work);
}

static inline void hfc_handle_fifo_tx_interrupt(struct hfc_fifo *fifo)
{
	if (fifo->connected_chan &&
	    visdn_leg_queue_stopped(&fifo->connected_chan->visdn_chan.leg_a)) {
		if (hfc_fifo_free_frames(fifo) &&
		    hfc_fifo_free_tx(fifo) > 20)
			visdn_leg_wake_queue(
				&fifo->connected_chan->visdn_chan.leg_a);
	}
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
	u8 status;
	u8 s1;
	u8 s2;

	if (unlikely(!card)) {
		hfc_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

	status = hfc_inb(card, hfc_STATUS);
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

	s1 = hfc_inb(card, hfc_INT_S1);
	s2 = hfc_inb(card, hfc_INT_S2);

	if (s1 != 0) {
		if (s1 & hfc_INT_S1_TIMER)
			hfc_handle_timer_interrupt(card);

		if (s1 & hfc_INT_S1_L1STATE)
			hfc_handle_state_interrupt(&card->st_port);

		if (s1 & hfc_INT_S1_DREC)
			hfc_handle_fifo_rx_interrupt(
				&card->fifos[hfc_FIFO_D][RX]);

		if (s1 & hfc_INT_S1_B1REC)
			hfc_handle_fifo_rx_interrupt(
				&card->fifos[hfc_FIFO_B1][RX]);

		if (s1 & hfc_INT_S1_B2REC)
			hfc_handle_fifo_rx_interrupt(
				&card->fifos[hfc_FIFO_B2][RX]);

		if (s1 & hfc_INT_S1_DTRANS)
			hfc_handle_fifo_tx_interrupt(
				&card->fifos[hfc_FIFO_D][TX]);

		if (s1 & hfc_INT_S1_B1TRANS)                                     
			hfc_handle_fifo_tx_interrupt(
				&card->fifos[hfc_FIFO_B1][TX]);

		if (s1 & hfc_INT_S1_B2TRANS)                                     
			hfc_handle_fifo_tx_interrupt(
				&card->fifos[hfc_FIFO_B2][TX]);
	}

	if (s2 != 0) {
		if (s2 & hfc_INT_S2_PCISTUCK) {

			/* CologneChip says:
			 *
			 * the meaning of this fatal error bit is that HFC-S
			 * PCI as PCI master could not access the PCI bus
			 * within 125us to finish its data processing. If this
			 * happens only very seldom it does not cause big
			 * problems but of course some B-channel or D-channel
			 * data will be corrupted due to this event.
			 * Unfortunately this bit is only set once after the
			 * problem occurs and can only be reseted by a
			 * software reset. That means it is not easily
			 * possible to check how often this fatal error happens.
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

int __devinit hfc_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	int err;

	struct hfc_card *card;
	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		hfc_msg(KERN_CRIT, "unable to kmalloc!\n");
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	memset(card, 0, sizeof(*card));

	spin_lock_init(&card->lock);

	card->pci_dev = pci_dev;
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

	card->io_bus_mem = pci_resource_start(pci_dev, 1);
	if (!card->io_bus_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	card->io_mem = ioremap(card->io_bus_mem, pci_resource_len(pci_dev, 1));
	if(!card->io_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	/* pci_alloc_consistent guarantees alignment
	 * (Documentation/DMA-mapping.txt)
	 */

	card->fifo_mem = pci_alloc_consistent(pci_dev, hfc_FIFO_SIZE,
						&card->fifo_bus_mem);
	if (!card->fifo_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"unable to allocate FIFO DMA memory!\n");
		err = -ENOMEM;
		goto err_alloc_fifo;
	}

	memset(card->fifo_mem, 0, hfc_FIFO_SIZE);

	pci_write_config_dword(card->pci_dev, hfc_PCI_MWBA, card->fifo_bus_mem);

	err = request_irq(card->pci_dev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card);
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT,
			"unable to register irq\n");
		goto err_request_irq;
	}

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_D][RX],
		card,
		D, RX,
		0x4000,
		0x4000,
		0x6080, 0x6082,
		0x0000, 0x01FF,
		0x10, 0x1F,
		0x60a0, 0x60a1);

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_D][TX],
		card,
		D, TX,
		0x0000,
		0x0000,
		0x2080, 0x2082,
		0x0000, 0x01FF,
		0x10, 0x1F,
		0x20a0, 0x20a1);

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_B1][RX],
		card,
		B1, RX,
		0x4200,
		0x4000,
		0x6000, 0x6002,
		0x0200, 0x1FFF,
		0x00, 0x1F,
		0x6080, 0x6081);

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_B1][TX],
		card,
		B1, TX,
		0x0200,
		0x0000,
		0x2000, 0x2002,
		0x0200, 0x1FFF,
		0x00, 0x1F,
		0x2080, 0x2081);

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_B2][RX],
		card,
		B2, RX,
		0x6200,
		0x6000,
		0x6100, 0x6102,
		0x0200, 0x1FFF,
		0x00, 0x1F,
		0x6180, 0x6181);

	hfc_fifo_init(
		&card->fifos[hfc_FIFO_B2][TX],
		card,
		B2, TX,
		0x2200,
		0x2000,
		0x2100, 0x2102,
		0x0200, 0x1FFF,
		0x00, 0x1F,
		0x2180, 0x2181);

	hfc_st_port_init(&card->st_port, card, "st0");
	hfc_pcm_port_init(&card->pcm_port, card, "pcm");

	hfc_card_lock(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	// Ok, the hardware is ready and the data structures are initialized,
	// we can now register to the system.

	err = hfc_st_port_register(&card->st_port);
	if (err < 0)
		goto err_st_port_register;

	err = hfc_pcm_port_register(&card->pcm_port);
	if (err < 0)
		goto err_pcm_port_register;

// -------------------------------------------------------

	err = hfc_card_sysfs_create_files(card);
	if (err < 0)
		goto err_card_sysfs_create_files;

	hfc_msg_card(card, KERN_INFO,
		"configured at mem %#lx (0x%p) IRQ %u\n",
		card->io_bus_mem,
		card->io_mem,
		card->pci_dev->irq);

	return 0;

	hfc_card_sysfs_delete_files(card);
err_card_sysfs_create_files:
	hfc_pcm_port_unregister(&card->pcm_port);
err_pcm_port_register:
	hfc_st_port_unregister(&card->st_port);
err_st_port_register:
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
	pci_set_drvdata(pci_dev, NULL);
	kfree(card);
err_alloc_hfccard:

	return err;
}

void __devexit hfc_card_remove(struct hfc_card *card)
{
	hfc_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n",
		card->io_mem);

	hfc_card_lock(card);
	hfc_softreset(card);
	hfc_card_unlock(card);

	hfc_card_sysfs_delete_files(card);

	hfc_pcm_port_unregister(&card->pcm_port);
	hfc_st_port_unregister(&card->st_port);

	// disable memio and bustmaster
	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	free_irq(card->pci_dev->irq, card);

	pci_free_consistent(card->pci_dev, hfc_FIFO_SIZE,
		card->fifo_mem, card->fifo_bus_mem);

	iounmap(card->io_mem);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);

	kfree(card);
}

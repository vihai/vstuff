/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
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
#include <linux/workqueue.h>

#include <visdn.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "st_port_sysfs.h"
#include "pcm_port.h"
#include "pcm_port_inline.h"
#include "pcm_port_sysfs.h"
#include "chan.h"
#include "chan_sysfs.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "card.h"
#include "card_sysfs.h"
#include "card_inline.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id hfc_pci_ids[] = {
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_8S,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hfc_pci_ids);

static int __devinit hfc_probe(struct pci_dev *dev
			, const struct pci_device_id *ent);
static void __devexit hfc_remove(struct pci_dev *dev);

static struct pci_driver hfc_driver = {
	.name     = hfc_DRIVER_NAME,
	.id_table = hfc_pci_ids,
	.probe    = hfc_probe,
	.remove   = hfc_remove,
};

static struct hfc_fifo_config hfc_fifo_config[] = {
	{ 0, 0, 0, 0x00, 0x0f, 32, 32,
		{ 0x0080, 0x01ff },
		{ 0, } },
	{ 0, 2, 0, 0x00, 0x0f, 16, 32,
		{ 0x0080, 0x00ff },
		{ 0x0000, 0x01ff } },
	{ 0, 2, 1, 0x00, 0x0f, 24, 32,
		{ 0x0080, 0x00ff },
		{ 0x0000, 0x03ff } },
	{ 0, 2, 2, 0x00, 0x0f, 28, 32,
		{ 0x0080, 0x00ff },
		{ 0x0000, 0x07ff } },
	{ 0, 2, 3, 0x00, 0x0f, 30, 32,
		{ 0x0080, 0x00ff },
		{ 0x0000, 0x0fff } },
	{ 0, 3, 0, 0x00, 0x0f, 16, 32,
		{ 0x0000, 0x00ff },
		{ 0x0000, 0x01ff } },
	{ 0, 3, 1, 0x00, 0x0f, 8, 16,
		{ 0x0000, 0x01ff },
		{ 0x0000, 0x03ff } },
	{ 0, 3, 2, 0x00, 0x0f, 4, 8,
		{ 0x0000, 0x03ff },
		{ 0x0000, 0x07ff } },
	{ 0, 3, 3, 0x00, 0x0f, 2, 4,
		{ 0x0000, 0x07ff },
		{ 0x0000, 0x0fff } },

	{ 1, 0, 0, 0x00, 0x1f, 32, 32,
		{ 0x00c0, 0x07ff },
		{ 0, } },
	{ 1, 2, 0, 0x00, 0x1f, 16, 32,
		{ 0x00c0, 0x03ff },
		{ 0x0000, 0x07ff } },
	{ 1, 2, 1, 0x00, 0x1f, 24, 32,
		{ 0x00c0, 0x03ff },
		{ 0x0000, 0x0fff } },
	{ 1, 2, 2, 0x00, 0x1f, 28, 32,
		{ 0x00c0, 0x03ff },
		{ 0x0000, 0x1fff } },
	{ 1, 2, 3, 0x00, 0x1f, 30, 32,
		{ 0x00c0, 0x03ff },
		{ 0x0000, 0x3fff } },
	{ 1, 3, 0, 0x00, 0x1f, 16, 32,
		{ 0x0000, 0x03ff },
		{ 0x0000, 0x07ff } },
	{ 1, 3, 1, 0x00, 0x1f, 8, 16,
		{ 0x0000, 0x07ff },
		{ 0x0000, 0x0fff } },
	{ 1, 3, 2, 0x00, 0x1f, 4, 8,
		{ 0x0000, 0x0fff },
		{ 0x0000, 0x1fff } },
	{ 1, 3, 3, 0x00, 0x1f, 2, 4,
		{ 0x0000, 0x1fff },
		{ 0x0000, 0x3fff } },

	{ 2, 0, 0, 0x00, 0x1f, 32, 32,
		{ 0x00c0, 0x1fff },
		{ 0, } },
	{ 2, 2, 0, 0x00, 0x1f, 16, 32,
		{ 0x00c0, 0x0fff },
		{ 0x0000, 0x1fff } },
	{ 2, 2, 1, 0x00, 0x1f, 24, 32,
		{ 0x00c0, 0x0fff },
		{ 0x0000, 0x3fff } },
	{ 2, 2, 2, 0x00, 0x1f, 28, 32,
		{ 0x00c0, 0x0fff },
		{ 0x0000, 0x7fff } },
	{ 2, 2, 3, 0x00, 0x1f, 30, 32,
		{ 0x00c0, 0x0fff },
		{ 0x0000, 0xffff } },
	{ 2, 3, 0, 0x00, 0x1f, 16, 32,
		{ 0x0000, 0x0fff },
		{ 0x0000, 0x1fff } },
	{ 2, 3, 1, 0x00, 0x1f, 8, 16,
		{ 0x0000, 0x1fff },
		{ 0x0000, 0x3fff } },
	{ 2, 3, 2, 0x00, 0x1f, 4, 8,
		{ 0x0000, 0x3fff },
		{ 0x0000, 0x7fff } },
	{ 2, 3, 3, 0x00, 0x1f, 2, 4,
		{ 0x0000, 0x7fff },
		{ 0x0000, 0xffff } },
};


/******************************************
 * HW routines
 ******************************************/

void hfc_softreset(struct hfc_card *card)
{
	WARN_ON(atomic_read(&card->sem.count) > 0);

	hfc_msg_card(card, KERN_INFO, "resetting\n");

	mb();
	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_SRES);
	mb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();

	hfc_wait_busy(card);
}

static void hfc_configure_fifo(
	struct hfc_fifo *fifo,
	struct hfc_fifo_config *fcfg,
	struct hfc_fifo_zone_config *fzcfg)
{
	fifo->f_min = fcfg->f_min;
	fifo->f_max = fcfg->f_max;
	fifo->f_num = fcfg->f_max - fcfg->f_min + 1;

	fifo->z_min = fzcfg->z_min;
	fifo->z_max = fzcfg->z_max;
	fifo->fifo_size = fzcfg->z_max - fzcfg->z_min + 1;
}

void hfc_configure_fifos(
	struct hfc_card *card,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz)
{
	// Find the correct configuration:

	struct hfc_fifo_config *fcfg = NULL;
	int i;
	for (i=0; i<ARRAY_SIZE(hfc_fifo_config); i++) {
		if (hfc_fifo_config[i].v_ram_sz == v_ram_sz &&
		    hfc_fifo_config[i].v_fifo_md == v_fifo_md &&
		    hfc_fifo_config[i].v_fifo_sz == v_fifo_sz) {

			fcfg = &hfc_fifo_config[i];
			break;
		}
	}

	hfc_debug_card(card, 2, "Using FIFO config #%d\n", i);

	BUG_ON(!fcfg);

	card->num_fifos = fcfg->num_fifos;

	for (i=0; i<fcfg->num_fifos; i++) {
		struct hfc_fifo_zone_config *fzcfg;

		if (i < fcfg->zone2_start_id)
			fzcfg = &fcfg->zone1;
		else
			fzcfg = &fcfg->zone2;

		hfc_configure_fifo(&card->fifos[i][RX], fcfg, fzcfg);
		hfc_configure_fifo(&card->fifos[i][TX], fcfg, fzcfg);

		hfc_debug_card(card, 3, "FIFO %d zmin=%04x zmax=%04x fmin=%02x fmax=%02x\n",
		i,
		fzcfg->z_min,
		fzcfg->z_max,
		fcfg->f_min,
		fcfg->f_max);
	}
}

static void hfc_initialize_hw_nonsoft(struct hfc_card *card)
{
	// FIFO RAM configuration
	card->ram_size = 32;
	card->regs.ram_misc = hfc_R_RAM_MISC_V_RAM_SZ_32K;

	hfc_outb(card, hfc_R_RAM_MISC, card->regs.ram_misc);

	card->regs.fifo_md = hfc_R_FIFO_MD_V_FIFO_MD_00 |
				hfc_R_FIFO_MD_V_DF_MD_FSM |
				hfc_R_FIFO_MD_V_FIFO_SZ_00;
	hfc_outb(card, hfc_R_FIFO_MD, card->regs.fifo_md);

	hfc_configure_fifos(card, 0, 0, 0);

	// IRQ MISC config
	card->regs.irqmsk_misc = 0;
	hfc_outb(card, hfc_R_IRQMSK_MISC, card->regs.irqmsk_misc);

	// Normal clock mode
	card->regs.ctrl = hfc_R_CTRL_V_ST_CLK_DIV_4;
	hfc_outb(card, hfc_R_CTRL, card->regs.ctrl);

	hfc_outb(card, hfc_R_BRG_PCM_CFG,
		hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV_1_5 |
		hfc_R_BRG_PCM_CFG_V_ADDR_WRDLY_3NS);

	// Here is the place to configure:
	// R_CIRM
}

void hfc_update_pcm_md0(struct hfc_card *card, u8 otherbits)
{
	hfc_outb(card, hfc_R_PCM_MD0,
		otherbits |
		(card->pcm_port.master ?
			hfc_R_PCM_MD0_V_PCM_MD_MASTER :
			hfc_R_PCM_MD0_V_PCM_MD_SLAVE));
}

void hfc_update_pcm_md1(struct hfc_card *card)
{
	WARN_ON(atomic_read(&card->sem.count) > 0);

	u8 pcm_md1 = 0;
	if (card->pcm_port.bitrate == 0) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_2MBIT;
		card->pcm_port.num_slots = 32;
	} else if (card->pcm_port.bitrate == 1) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_4MBIT;
		card->pcm_port.num_slots = 64;
	} else if (card->pcm_port.bitrate == 2) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_8MBIT;
		card->pcm_port.num_slots = 64;
	}

	hfc_pcm_multireg_select(card, hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD1);
	hfc_outb(card, hfc_R_PCM_MD1, pcm_md1);
}

void hfc_update_st_sync(struct hfc_card *card)
{
	if (card->clock_source == -1)
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_AUTO_SYNC_ENABLED);
	else
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_SYNC_SEL(card->clock_source & 0x07) |
			hfc_R_ST_SYNC_V_AUTO_SYNC_DISABLED);
}

void hfc_update_bert_wd_md(struct hfc_card *card, u8 otherbits)
{
	hfc_outb(card, hfc_R_BERT_WD_MD,
		hfc_R_BERT_WD_MD_V_PAT_SEQ(card->bert_mode & 0x7) |
		otherbits);
}

void hfc_initialize_hw(struct hfc_card *card)
{
	WARN_ON(atomic_read(&card->sem.count) > 0);

	card->output_level = 0x19;
	card->clock_source = -1;
	card->bert_mode = 0;

	card->pcm_port.master = TRUE;
	card->pcm_port.bitrate = 0;

	hfc_outb(card, hfc_R_PWM_MD,
		hfc_R_PWM_MD_V_PWM0_MD_PUSH |
		hfc_R_PWM_MD_V_PWM1_MD_PUSH);

	hfc_outb(card, hfc_R_PWM1, card->output_level);

	// Timer setup
	hfc_outb(card, hfc_R_TI_WD,
		hfc_R_TI_WD_V_EV_TS_8_192_S);
//		hfc_R_TI_WD_V_EV_TS_1_MS);

	hfc_update_pcm_md0(card, 0);
	hfc_update_pcm_md1(card);
	hfc_update_st_sync(card);
	hfc_update_bert_wd_md(card, 0);

	hfc_outb(card, hfc_R_SCI_MSK,
		hfc_R_SCI_MSK_V_SCI_MSK_ST0|
		hfc_R_SCI_MSK_V_SCI_MSK_ST1|
		hfc_R_SCI_MSK_V_SCI_MSK_ST2|
		hfc_R_SCI_MSK_V_SCI_MSK_ST3|
		hfc_R_SCI_MSK_V_SCI_MSK_ST4|
		hfc_R_SCI_MSK_V_SCI_MSK_ST5|
		hfc_R_SCI_MSK_V_SCI_MSK_ST6|
		hfc_R_SCI_MSK_V_SCI_MSK_ST7);

/*	hfc_outb(card, hfc_R_GPIO_SEL,
		hfc_R_GPIO_SEL_V_GPIO_SEL6 |
		hfc_R_GPIO_SEL_V_GPIO_SEL7)*/


	// Timer interrupt enabled
	hfc_outb(card, hfc_R_IRQMSK_MISC,
		hfc_R_IRQMSK_MISC_V_TI_IRQMSK);

	int i;
	for (i=0; i<card->num_st_ports; i++) {
		hfc_st_port_select(&card->st_ports[i]);
		hfc_st_port_update_st_ctrl_0(&card->st_ports[i]);
		hfc_st_port_update_st_ctrl_2(&card->st_ports[i]);
		hfc_st_port_update_st_clk_dly(&card->st_ports[i]);
	}

	// Enable interrupts
	hfc_outb(card, hfc_R_IRQ_CTRL,
		hfc_R_IRQ_CTRL_V_FIFO_IRQ|
		hfc_R_IRQ_CTRL_V_GLOB_IRQ_EN|
		hfc_R_IRQ_CTRL_V_IRQ_POL_LOW);
}

struct hfc_fifo *hfc_allocate_fifo(
	struct hfc_card *card,
	enum hfc_direction direction)
{
	int i;
	for (i=0; i<card->num_fifos; i++) {
		if (!card->fifos[i][direction].used) {
			card->fifos[i][direction].used = TRUE;
			return &card->fifos[i][direction];
		}
	}

	return NULL;
}

void hfc_deallocate_fifo(struct hfc_fifo *fifo)
{
	fifo->used = FALSE;
}

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_fifo_tx_interrupt(struct hfc_fifo *fifo)
{
	if (fifo->connected_chan)
		visdn_pass_wake_queue(&fifo->connected_chan->chan->visdn_chan);
}

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_fifo *fifo)
{
	schedule_work(&fifo->work);
}

static inline void hfc_handle_timer_interrupt(struct hfc_card *card)
{
}

static inline void hfc_handle_state_interrupt(struct hfc_st_port *port)
{
	schedule_work(&port->state_change_work);
}

static inline void hfc_handle_fifo_block_interrupt(
	struct hfc_card *card, int block)
{
	u8 fifo_irq = hfc_inb(card,
		hfc_R_IRQ_FIFO_BL0 + block);

	if (fifo_irq & (1 << 0))
		hfc_handle_fifo_tx_interrupt(&card->fifos[block * 4 + 0][TX]);
	if (fifo_irq & (1 << 1))
		hfc_handle_fifo_rx_interrupt(&card->fifos[block * 4 + 0][RX]);
	if (fifo_irq & (1 << 2))
		hfc_handle_fifo_tx_interrupt(&card->fifos[block * 4 + 1][TX]);
	if (fifo_irq & (1 << 3))
		hfc_handle_fifo_rx_interrupt(&card->fifos[block * 4 + 1][RX]);
	if (fifo_irq & (1 << 4))
		hfc_handle_fifo_tx_interrupt(&card->fifos[block * 4 + 2][TX]);
	if (fifo_irq & (1 << 5))
		hfc_handle_fifo_rx_interrupt(&card->fifos[block * 4 + 2][RX]);
	if (fifo_irq & (1 << 6))
		hfc_handle_fifo_tx_interrupt(&card->fifos[block * 4 + 3][TX]);
	if (fifo_irq & (1 << 7))
		hfc_handle_fifo_rx_interrupt(&card->fifos[block * 4 + 3][RX]);
}

/*
 * Interrupt handling routine.
 *
 * NOTE: We must be careful to not change port/fifo/slot selection register,
 *       otherwise we may race with code protected only by semaphores.
 *
 */

static irqreturn_t hfc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct hfc_card *card = dev_id;

	u8 status = hfc_inb(card, hfc_R_STATUS);
	u8 irq_sci = hfc_inb(card, hfc_R_SCI);

	if (unlikely(
		!(status & (hfc_R_STATUS_V_MISC_IRQSTA |
		            hfc_R_STATUS_V_FR_IRQSTA)) &&
	    !irq_sci)) {
		// probably we are sharing the irq
		return IRQ_NONE;
	}

	if (status & hfc_R_STATUS_V_MISC_IRQSTA) {
		u8 irq_misc = hfc_inb(card, hfc_R_IRQ_MISC);

		if (irq_misc & hfc_R_IRQ_MISC_V_TI_IRQ) {
			hfc_handle_timer_interrupt(card);
		}
	}

	if (status & hfc_R_STATUS_V_FR_IRQSTA) {
		u8 irq_oview = hfc_inb(card, hfc_R_IRQ_OVIEW);

		int i;
		for (i=0; i<8; i++) {
			if (irq_oview & (1 << i)) {
				hfc_handle_fifo_block_interrupt(card, i);
			}
		}
	}

	if (irq_sci) {
		int i;
		for (i=0; i<card->num_st_ports; i++) {
			if (irq_sci & (1 << card->st_ports[i].id)) {
				hfc_handle_state_interrupt(&card->st_ports[i]);
			}
		}
	}

	return IRQ_HANDLED;
}

/******************************************
 * Module initialization and cleanup
 ******************************************/

static int __devinit hfc_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	int err;

	struct hfc_card *card = NULL;
	card = kmalloc(sizeof(struct hfc_card), GFP_KERNEL);
	if (!card) {
		hfc_msg(KERN_CRIT, "unable to kmalloc!\n");
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	memset(card, 0x00, sizeof(*card));

	init_MUTEX(&card->sem);

	card->pcidev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	// From here on hfc_msg_card may be used

	if ((err = pci_enable_device(pci_dev))) {
		goto err_pci_enable_device;
	}

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	if((err = pci_request_regions(pci_dev, hfc_DRIVER_NAME))) {
		hfc_msg_card(card, KERN_CRIT,
			"cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		hfc_msg_card(card, KERN_CRIT,
			"PCI device does not have an assigned IRQ!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev,1);
	if (!card->io_bus_mem) {
		hfc_msg_card(card, KERN_CRIT,
			"PCI device does not have an assigned IO memory area!\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	card->io_mem = ioremap(card->io_bus_mem, hfc_PCI_MEM_SIZE);
	if(!card->io_mem) {
		hfc_msg_card(card, KERN_CRIT, "cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	err = request_irq(card->pcidev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card);
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT, "unable to register irq\n");
		goto err_request_irq;
	}

	int chip_type = hfc_R_CHIP_ID_V_CHIP_ID(hfc_inb(card, hfc_R_CHIP_ID));

	if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_4S) {
		card->num_st_ports = 4;
	} else if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_8S) {
		card->num_st_ports = 8;
	} else {
		hfc_msg_card(card, KERN_ERR,
			"unknown chip type '0x%02x'\n", chip_type);

		goto err_unknown_chip;
	}

	int revision = hfc_R_CHIP_RV_V_CHIP_RV(hfc_inb(card, hfc_R_CHIP_RV));

	if (revision < 1)
		goto err_unsupported_revision;

	hfc_msg_card(card, KERN_ERR,
		"HFC-%cS chip rev. %02x detected\n",
			chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_4S ?
				'4' : '8',
			revision);

	// Initialize all the card's components

	int i;
	for (i=0; i<sizeof(card->fifos)/sizeof(*card->fifos); i++) {
		hfc_fifo_init(&card->fifos[i][RX], card, i, RX);
		hfc_fifo_init(&card->fifos[i][TX], card, i, TX);
	}
	card->num_fifos = 0;

	for (i=0; i<card->num_st_ports; i++) {
		char portid[10];
		snprintf(portid, sizeof(portid), "st%d", i);

		hfc_st_port_init(&card->st_ports[i], card, portid, i);
	}

	hfc_pcm_port_init(&card->pcm_port, card, "pcm");

	hfc_card_lock(card);
	hfc_initialize_hw_nonsoft(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	// Ok, the hardware is ready and the data structures are initialized,
	// we can now register to the system.

	for (i=0; i<card->num_st_ports; i++) {

		visdn_port_register(&card->st_ports[i].visdn_port);

		visdn_chan_register(&card->st_ports[i].chans[D].visdn_chan);
		hfc_chan_sysfs_create_files_D(&card->st_ports[i].chans[D]);

		visdn_chan_register(&card->st_ports[i].chans[B1].visdn_chan);
		hfc_chan_sysfs_create_files_B(&card->st_ports[i].chans[B1]);

		visdn_chan_register(&card->st_ports[i].chans[B2].visdn_chan);
		hfc_chan_sysfs_create_files_B(&card->st_ports[i].chans[B2]);

		visdn_chan_register(&card->st_ports[i].chans[E].visdn_chan);
		hfc_chan_sysfs_create_files_E(&card->st_ports[i].chans[E]);

		visdn_chan_register(&card->st_ports[i].chans[SQ].visdn_chan);
		hfc_chan_sysfs_create_files_SQ(&card->st_ports[i].chans[SQ]);

		hfc_st_port_sysfs_create_files(&card->st_ports[i]);
	}

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

	visdn_port_unregister(&card->st_ports[3].visdn_port);
	visdn_port_unregister(&card->st_ports[2].visdn_port);
	visdn_port_unregister(&card->st_ports[1].visdn_port);
	visdn_port_unregister(&card->st_ports[0].visdn_port);

	hfc_card_sysfs_delete_files(card);
err_card_sysfs_create_files:
err_unsupported_revision:
err_unknown_chip:
	free_irq(pci_dev->irq, card);
err_request_irq:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
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

	int i;
	for (i=card->num_st_ports - 1; i>=0; i--) {
		visdn_chan_unregister(&card->st_ports[i].chans[SQ].visdn_chan);
		visdn_chan_unregister(&card->st_ports[i].chans[E].visdn_chan);
		visdn_chan_unregister(&card->st_ports[i].chans[B2].visdn_chan);
		visdn_chan_unregister(&card->st_ports[i].chans[B1].visdn_chan);
		visdn_chan_unregister(&card->st_ports[i].chans[D].visdn_chan);
		visdn_port_unregister(&card->st_ports[i].visdn_port);
	}

	// softreset clears all pending interrupts
	hfc_card_lock(card);
	hfc_softreset(card);
	hfc_card_unlock(card);

	// There should be no interrupt from here on

	pci_write_config_word(pci_dev, PCI_COMMAND, 0);
	free_irq(pci_dev->irq, card);
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
MODULE_PARM(debug_level,"i");
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

/*
 * hfc-4s_main.c - vISDN driver for HFC-4S based cards
 *
 * Copyright (C) 2004 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 * Please read the README file for important infos.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>

#include <lapd.h>
#include <visdn.h>

#include "hfc-4s.h"
#include "fifo.h"

#if CONFIG_PCI

#define to_port(port) container_of(port, struct hfc_port, visdn_port)
#define to_chan_duplex(chan) container_of(chan, struct hfc_chan_duplex, visdn_chan)

static int force_l1_up = 0;
static struct proc_dir_entry *hfc_proc_hfc_dir;

#ifdef DEBUG
int debug_level = 0;
#endif

static struct pci_device_id hfc_pci_ids[] = {
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_08B4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_16B8,
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

static inline void hfc_select_port(struct hfc_card *card, u8 id)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	hfc_outb(card, hfc_R_ST_SEL,
		hfc_R_ST_SEL_V_ST_SEL(id));

	mb();
}

static void hfc_softreset(struct hfc_card *card)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	hfc_msg_card(KERN_INFO, card, "resetting\n");

	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_SRES);
	wmb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();

	hfc_wait_busy(card);
}

static void hfc_chan_disable(struct hfc_chan_duplex *chan)
{
	struct hfc_card *card = chan->port->card;

	WARN_ON(!irqs_disabled() && !in_irq());

	hfc_select_port(card, chan->port->id);

	if (chan->id == B1 || chan->id == B2) {
		if (chan->id == B1) {
			chan->port->regs.st_ctrl_0 &= ~hfc_A_ST_CTRL0_V_B1_EN;
			chan->port->regs.st_ctrl_2 &= ~hfc_A_ST_CTRL2_V_B1_RX_EN;
		} else if (chan->id == B2) {
			chan->port->regs.st_ctrl_0 &= ~hfc_A_ST_CTRL0_V_B2_EN;
			chan->port->regs.st_ctrl_2 &= ~hfc_A_ST_CTRL2_V_B2_RX_EN;
		}

		hfc_outb(card, hfc_A_ST_CTRL0,
			chan->port->regs.st_ctrl_0);
		hfc_outb(card, hfc_A_ST_CTRL2,
			chan->port->regs.st_ctrl_2);
	}

	// RX
	chan->rx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_IFF|
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	hfc_outb(card, hfc_A_IRQ_MSK, 0);

	// TX
	chan->tx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_IFF|
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	hfc_outb(card, hfc_A_IRQ_MSK, 0);
}

void hfc__do_set_role(struct hfc_port *port, int nt_mode)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	if (nt_mode) {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_NT;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0C;
		port->sampling_comp = 0x6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));
	} else {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_TE;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0E;
		port->sampling_comp = 6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));
	}

	port->visdn_port.nt_mode = nt_mode;
}

static void hfc_softreset(struct hfc_card *card);
static void hfc_initialize_hw(struct hfc_card *card);

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

static void hfc_configure_fifos(
	struct hfc_card *card,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz)
{
	// Find the correct configuration:

	struct hfc_fifo_config *fcfg = NULL;
	int i;
	for (i=0; i<sizeof(hfc_fifo_config)/sizeof(*hfc_fifo_config); i++) {
		if (hfc_fifo_config[i].v_ram_sz == v_ram_sz &&
		    hfc_fifo_config[i].v_fifo_md == v_fifo_md &&
		    hfc_fifo_config[i].v_fifo_sz == v_fifo_sz) {

			fcfg = &hfc_fifo_config[i];
			break;
		}
	}
	hfc_debug_card(2, card, "Using FIFO config #%d\n", i);

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

printk(KERN_INFO "FIFO %d zmin=%04x zmax=%04x fmin=%02x fmax=%02x\n",
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
	card->ramsize = 32;
	card->regs.ram_misc = hfc_R_RAM_MISC_V_RAM_SZ_32K;

	hfc_outb(card, hfc_R_RAM_MISC, card->regs.ram_misc);

	card->regs.fifo_md = hfc_R_FIFO_MD_V_FIFO_MD_00 |
				hfc_R_FIFO_MD_V_DF_MD_SM |
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

static void hfc_initialize_hw(struct hfc_card *card)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	hfc_outb(card, hfc_R_PWM_MD,
		hfc_R_PWM_MD_V_PWM0_MD_PUSH |
		hfc_R_PWM_MD_V_PWM1_MD_PUSH);

	card->output_level = 0x19;
	hfc_outb(card, hfc_R_PWM1, card->output_level);

	// Timer setup
	hfc_outb(card, hfc_R_TI_WD,
		hfc_R_TI_WD_V_EV_TS_8_192_S);
//		hfc_R_TI_WD_V_EV_TS_1_MS);

	// C4IO F0IO are outputs (master mode) TODO: Slave mode
	hfc_outb(card, hfc_R_PCM_MD0,
		hfc_R_PCM_MD0_V_PCM_MD);

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

	// Automatic synchronization
	hfc_outb(card, hfc_R_ST_SYNC,
		hfc_R_ST_SYNC_V_AUTO_SYNC_ENABLED);
	card->clock_source = -1;

	card->bert_mode = 0;
	card->regs.bert_wd_md = 0;
	hfc_outb(card, hfc_R_BERT_WD_MD, card->regs.bert_wd_md);

	// Timer interrupt enabled
	hfc_outb(card, hfc_R_IRQMSK_MISC,
		hfc_R_IRQMSK_MISC_V_TI_IRQMSK);

	// Enable interrupts
	hfc_outb(card, hfc_R_IRQ_CTRL,
		hfc_R_IRQ_CTRL_V_FIFO_IRQ|
		hfc_R_IRQ_CTRL_V_GLOB_IRQ_EN|
		hfc_R_IRQ_CTRL_V_IRQ_POL_LOW);
}

static void hfc_update_fifo_state(struct hfc_card *card)
{
	// I'm not sure if irqsave is needed but there could be a race
	// condition since hfc_update_fifo_state could be called from
	// both the IRQ handler and the *_(open|close) functions

/*	unsigned long flags;
	spin_lock_irqsave(&card->chans[B1].lock, flags);
	if (!card->fifo_suspended &&
		(card->chans[B1].status == open_framed ||
		card->chans[B1].status == open_trans)) {

 	 	if(!(card->regs.fifo_en & hfc_FIFOEN_B1RX)) {
			card->regs.fifo_en |= hfc_FIFOEN_B1RX;
			hfc_fifo_reset(&card->chans[B1].rx);
		}

 	 	if(!(card->regs.fifo_en & hfc_FIFOEN_B1TX)) {
			card->regs.fifo_en |= hfc_FIFOEN_B1TX;
			hfc_fifo_reset(&card->chans[B1].tx);
		}
	} else {
 	 	if(card->regs.fifo_en & hfc_FIFOEN_B1RX)
			card->regs.fifo_en &= ~hfc_FIFOEN_B1RX;
 	 	if(card->regs.fifo_en & hfc_FIFOEN_B1TX)
			card->regs.fifo_en &= ~hfc_FIFOEN_B1TX;
	}
	spin_unlock_irqrestore(&card->chans[B1].lock, flags);

	spin_lock_irqsave(&card->chans[B2].lock, flags);
	if (!card->fifo_suspended &&
		(card->chans[B2].status == open_framed ||
		card->chans[B2].status == open_trans ||
		card->chans[B2].status == sniff_aux)) {

 	 	if(!(card->regs.fifo_en & hfc_FIFOEN_B2RX)) {
			card->regs.fifo_en |= hfc_FIFOEN_B2RX;
			hfc_fifo_reset(&card->chans[B2].rx);
		}

 	 	if(!(card->regs.fifo_en & hfc_FIFOEN_B2TX)) {
			card->regs.fifo_en |= hfc_FIFOEN_B2TX;
			hfc_fifo_reset(&card->chans[B2].tx);
		}
	} else {
 	 	if(card->regs.fifo_en & hfc_FIFOEN_B2RX)
			card->regs.fifo_en &= ~hfc_FIFOEN_B2RX;
 	 	if(card->regs.fifo_en & hfc_FIFOEN_B2TX)
			card->regs.fifo_en &= ~hfc_FIFOEN_B2TX;
	}
	spin_unlock_irqrestore(&card->chans[B2].lock, flags);

	spin_lock_irqsave(&card->chans[D].lock, flags);
	if (!card->fifo_suspended &&
		card->chans[D].status == open_framed) {

 	 	if(!(card->regs.fifo_en & hfc_FIFOEN_DTX)) {
			card->regs.fifo_en |= hfc_FIFOEN_DTX;
		}
	} else {
// 	 	if(card->regs.fifo_en & hfc_FIFOEN_DRX)
//			card->regs.fifo_en &= ~hfc_FIFOEN_DRX;
 	 	if(card->regs.fifo_en & hfc_FIFOEN_DTX)
			card->regs.fifo_en &= ~hfc_FIFOEN_DTX;
	}
	spin_unlock_irqrestore(&card->chans[D].lock, flags);

	hfc_outb(card, hfc_FIFO_EN, card->regs.fifo_en);*/
}

static inline void hfc_suspend_fifo(struct hfc_card *card)
{
//	card->fifo_suspended = TRUE;

	hfc_update_fifo_state(card);

	// When L1 goes down D rx receives garbage; it is nice to
	// clear it to avoid a CRC error on reactivation
	// udelay is needed because the FIFO deactivation happens
	// in 250us
	udelay(250);
//	hfc_fifo_reset(&card->chans[D].rx);

	hfc_debug_card(3, card, "FIFOs suspended\n");
}

static inline void hfc_resume_fifo(struct hfc_card *card)
{
//	card->fifo_suspended = FALSE;

	hfc_update_fifo_state(card);

	hfc_debug_card(3, card, "FIFOs resumed\n");
}

static void hfc_check_l1_up(struct hfc_port *port)
{
	struct hfc_card *card = port->card;

	if ((!port->visdn_port.nt_mode && port->l1_state != 7) ||
		(port->visdn_port.nt_mode && port->l1_state != 3)) {

		hfc_debug(1,
			"card %d: "
			"port %d: "
			"L1 is down, bringing up L1.\n",
			card->id,
			port->id);

		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);

       	}
}

/******************************************
 * sysfs interface functions
 ******************************************/

static ssize_t hfc_unsupported_store(
	struct device *device,
	const char *buf,
	size_t count)
{
	return -EOPNOTSUPP;
}

//----------------------------------------------------------------------------
static ssize_t hfc_show_clock_source_config(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	if (card->clock_source >= 0)
		return snprintf(buf, PAGE_SIZE, "%d\n", card->clock_source);
	else
		return snprintf(buf, PAGE_SIZE, "auto\n");
}

static ssize_t hfc_store_clock_source_config(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	
	unsigned long flags;

	if (count >= 4 && !strncmp(buf, "auto", 4)) {
		spin_lock_irqsave(&card->lock, flags);
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_AUTO_SYNC_ENABLED);
		card->clock_source = -1;

		spin_unlock_irqrestore(&card->lock, flags);

		hfc_debug_card(1, card, "Clock source set to auto\n");
	} else if(count > 0) {
		int clock_source;
		sscanf(buf, "%d", &clock_source);

		spin_lock_irqsave(&card->lock, flags);
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_SYNC_SEL(clock_source & 0x07) |
			hfc_R_ST_SYNC_V_AUTO_SYNC_DISABLED);
		card->clock_source = clock_source;
		spin_unlock_irqrestore(&card->lock, flags);

		hfc_debug_card(1, card, "Clock source set to %d\n", clock_source);
	}

	return count;
}

static DEVICE_ATTR(clock_source_config, S_IRUGO | S_IWUSR,
		hfc_show_clock_source_config,
		hfc_store_clock_source_config);

//----------------------------------------------------------------------------
static ssize_t hfc_show_clock_source_current(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		hfc_inb(card, hfc_R_BERT_STA) &
			hfc_R_BERT_STA_V_RD_SYNC_SRC_MASK);
}

static DEVICE_ATTR(clock_source_current, S_IRUGO,
		hfc_show_clock_source_current,
		hfc_unsupported_store);

//----------------------------------------------------------------------------
static ssize_t hfc_show_dip_switches(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n",
		hfc_inb(card, hfc_R_GPIO_IN1) >> 5);
}

static DEVICE_ATTR(dip_switches, S_IRUGO,
		hfc_show_dip_switches,
		hfc_unsupported_store);

//----------------------------------------------------------------------------
static ssize_t hfc_show_l1_state(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%c%d\n",
		port->visdn_port.nt_mode?'G':'F',
		port->l1_state);
}

static ssize_t hfc_store_l1_state(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);
	struct hfc_card *card = port->card;
	int err;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_select_port(card, port->id);

	if (count >= 8 && !strncmp(buf, "activate", 8)) {
		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);
	} else if (count >= 10 && !strncmp(buf, "deactivate", 10)) {
		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_DEACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);
	} else {
		int state;
		if (sscanf(buf, "%d", &state) < 1) {
			err = -EINVAL;
			goto err_invalid_scanf;
		}

		if (state < 0 ||
		    (port->visdn_port.nt_mode && state > 7) ||
		    (!port->visdn_port.nt_mode && state > 3)) {
			err = -EINVAL;
			goto err_invalid_state;
		}

		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_SET_STA(state));
	}

	spin_unlock_irqrestore(&card->lock, flags);

	return count;

err_invalid_scanf:
err_invalid_state:

	spin_unlock_irqrestore(&card->lock, flags);

	return err;
}

static DEVICE_ATTR(l1_state, S_IRUGO | S_IWUSR,
		hfc_show_l1_state,
		hfc_store_l1_state);

//----------------------------------------------------------------------------
static ssize_t hfc_show_st_clock_delay(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->clock_delay);
}

static ssize_t hfc_store_st_clock_delay(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	port->clock_delay = value;
	hfc_select_port(card, port->id);
	port->regs.st_clk_dly &= ~hfc_A_ST_CLK_DLY_V_ST_CLK_DLY_MSK;
	port->regs.st_clk_dly |= hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay);
	hfc_outb(card, hfc_A_ST_CLK_DLY, port->regs.st_clk_dly);

	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(st_clock_delay, S_IRUGO | S_IWUSR,
		hfc_show_st_clock_delay,
		hfc_store_st_clock_delay);

//----------------------------------------------------------------------------
static ssize_t hfc_show_st_sampling_comp(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->sampling_comp);
}

static ssize_t hfc_store_st_sampling_comp(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_port *port = to_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	if (value > 0x7)
		return -EINVAL;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	port->sampling_comp = value;
	hfc_select_port(card, port->id);
	port->regs.st_clk_dly &= ~hfc_A_ST_CLK_DLY_V_ST_SMPL_MSK;
	port->regs.st_clk_dly |=
		hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp);
	hfc_outb(card, hfc_A_ST_CLK_DLY, port->regs.st_clk_dly);
	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(st_sampling_comp, S_IRUGO | S_IWUSR,
		hfc_show_st_sampling_comp,
		hfc_store_st_sampling_comp);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_enabled(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->status == open_bert ? 1 : 0);
}

static ssize_t hfc_store_bert_enabled(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;
	
	unsigned long flags;

	int enabled;
	sscanf(buf, "%d", &enabled);

	spin_lock_irqsave(&card->lock, flags);

// FIXME TODO one-way only

	if (enabled) {
		if (chan->status != free) {
			spin_unlock_irqrestore(&card->lock, flags);
			return -EBUSY;
		}

		chan->status = open_bert;

		if (chan->id == B1 || chan->id == B2) {
			hfc_select_port(card, chan->port->id);

			if (chan->id == B1) {
				chan->port->regs.st_ctrl_0 |= hfc_A_ST_CTRL0_V_B1_EN;
				chan->port->regs.st_ctrl_2 |= hfc_A_ST_CTRL2_V_B1_RX_EN;
			} else if (chan->id == B2) {
				chan->port->regs.st_ctrl_0 |= hfc_A_ST_CTRL0_V_B2_EN;
				chan->port->regs.st_ctrl_2 |= hfc_A_ST_CTRL2_V_B2_RX_EN;
			}

			hfc_outb(card, hfc_A_ST_CTRL0,
				chan->port->regs.st_ctrl_0);
			hfc_outb(card, hfc_A_ST_CTRL2,
				chan->port->regs.st_ctrl_2);
		}

		// RX
		chan->rx.fifo->bit_reversed = FALSE;
		hfc_fifo_select(chan->rx.fifo);
		hfc_fifo_reset(chan->rx.fifo);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		if (chan->id == D) {
			hfc_outb(card, hfc_A_SUBCH_CFG,
				hfc_A_SUBCH_CFG_V_BIT_CNT_2);
		} else {
			hfc_outb(card, hfc_A_SUBCH_CFG,
				hfc_A_SUBCH_CFG_V_BIT_CNT_8);
		}

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_BERT_EN);

		// TX
		chan->tx.fifo->bit_reversed = FALSE;
		hfc_fifo_select(chan->tx.fifo);
		hfc_fifo_reset(chan->tx.fifo);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

		if (chan->id == D) {
			hfc_outb(card, hfc_A_SUBCH_CFG,
				hfc_A_SUBCH_CFG_V_BIT_CNT_2);
		} else {
			hfc_outb(card, hfc_A_SUBCH_CFG,
				hfc_A_SUBCH_CFG_V_BIT_CNT_8);
		}

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_BERT_EN);

		hfc_msg_chan(KERN_INFO, chan, "BERT enabled\n");
	} else {
		if (chan->status == open_bert) {
			hfc_chan_disable(chan);

			chan->status = free;

			hfc_msg_chan(KERN_INFO, chan, "BERT disabled\n");
		}
	}

	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(bert_enabled, S_IRUGO | S_IWUSR,
		hfc_show_bert_enabled,
		hfc_store_bert_enabled);

//----------------------------------------------------------------------------

static ssize_t hfc_show_output_level(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n", card->output_level);
}

static ssize_t hfc_store_output_level(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	
	int output_level;
	sscanf(buf, "%x", &output_level);

	if (output_level < 0 || output_level > 0xff)
		return -EINVAL;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	hfc_outb(card, hfc_R_PWM1, output_level);
	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(output_level, S_IRUGO | S_IWUSR,
		hfc_show_output_level,
		hfc_store_output_level);

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_mode(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->bert_mode);
}

static ssize_t hfc_store_bert_mode(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	
	int mode;
	sscanf(buf, "%d", &mode);

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	card->regs.bert_wd_md &= ~hfc_R_BERT_WD_MD_V_PAT_SEQ_MASK;
	card->regs.bert_wd_md |= hfc_R_BERT_WD_MD_V_PAT_SEQ(mode & 0x7);
	hfc_outb(card, hfc_R_BERT_WD_MD, card->regs.bert_wd_md);
	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(bert_mode, S_IRUGO | S_IWUSR,
		hfc_show_bert_mode,
		hfc_store_bert_mode);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_err(
	struct device *device,
	char *buf)
{
	return -EOPNOTSUPP;
}

static ssize_t hfc_store_bert_err(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_outb(card, hfc_R_BERT_WD_MD,
		card->regs.bert_wd_md | hfc_R_BERT_WD_MD_V_BERT_ERR);

	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(bert_err, S_IWUSR,
		hfc_show_bert_err,
		hfc_store_bert_err);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_sync(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(hfc_inb(card, hfc_R_BERT_STA) &
			hfc_R_BERT_STA_V_BERT_SYNC) ? 1 : 0);
}

static DEVICE_ATTR(bert_sync, S_IRUGO,
		hfc_show_bert_sync,
		hfc_unsupported_store);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_inv(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(hfc_inb(card, hfc_R_BERT_STA) &
			hfc_R_BERT_STA_V_BERT_INV_DATA) ? 1 : 0);
}

static DEVICE_ATTR(bert_inv, S_IRUGO,
		hfc_show_bert_inv,
		hfc_unsupported_store);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_cnt(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int cnt;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	cnt = hfc_inb(card, hfc_R_BERT_ECL);
	cnt += hfc_inb(card, hfc_R_BERT_ECH) << 8;
	spin_unlock_irqrestore(&card->lock, flags);

	return snprintf(buf, PAGE_SIZE, "%d\n", cnt);
}

static DEVICE_ATTR(bert_cnt, S_IRUGO,
		hfc_show_bert_cnt,
		hfc_unsupported_store);

//----------------------------------------------------------------------------

static ssize_t hfc_show_ramsize(
	struct device *device,
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d\n",
		card->ramsize);
}

static ssize_t hfc_store_ramsize(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);
	
	unsigned long flags;

	int ramsize;
	
	if (!sscanf(buf, "%d", &ramsize))
		return -EINVAL;

	if (ramsize != 32 ||
	    ramsize != 128 ||
	    ramsize != 512)
		return -EINVAL;

	if (ramsize != card->ramsize) {
		spin_lock_irqsave(&card->lock, flags);

		card->regs.ctrl &= ~hfc_R_CTRL_V_EXT_RAM;
		card->regs.ram_misc &= ~hfc_R_RAM_MISC_V_RAM_SZ_MASK;

		if (ramsize == 32) {
			card->regs.ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_32K;
			hfc_configure_fifos(card, 0, 0, 0);
		} else if (ramsize == 128) {
			card->regs.ctrl |= hfc_R_CTRL_V_EXT_RAM;
			card->regs.ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_128K;
			hfc_configure_fifos(card, 1, 0, 0);
		} else if (ramsize == 512) {
			card->regs.ctrl |= hfc_R_CTRL_V_EXT_RAM;
			card->regs.ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_512K;
			hfc_configure_fifos(card, 2, 0, 0);
		}

		hfc_outb(card, hfc_R_CTRL, card->regs.ctrl);
		hfc_outb(card, hfc_R_RAM_MISC, card->regs.ram_misc);

		hfc_softreset(card);
		hfc_initialize_hw(card);

		card->ramsize = ramsize;

		spin_unlock_irqrestore(&card->lock, flags);

		hfc_debug_card(1, card, "RAM size set to %d\n", ramsize);
	}

	return count;
}

static DEVICE_ATTR(ramsize, S_IRUGO | S_IWUSR,
		hfc_show_ramsize,
		hfc_store_ramsize);



/******************************************
 * /proc interface functions
 ******************************************/

struct hfc_status_to_name_names {
	enum hfc_chan_status status;
	char *name;
};

static char *hfc_status_to_name(int status)
{
	struct hfc_status_to_name_names names[] = {
		{ free, "free" },
		{ open_hdlc, "framed" },
		{ open_trans, "transparent" },
		{ sniff_aux, "sniff aux" },
		{ loopback, "loopback" },
		};

	int i;

	for (i=0; i<sizeof(names); i++) {
		if (names[i].status == status)
			return names[i].name;
	}

	return "*UNK*";
}

static int hfc_proc_read_info(char *page, char **start,
		off_t off, int count, 
		int *eof, void *data)
{
//	struct hfc_card *card = data;

//	u8 chip_id;
//	chip_id = hfc_inb(card, hfc_CHIP_ID);

	int len;

	len=0;
/*	len = snprintf(page, PAGE_SIZE,
		"Cardnum   : %d\n"
		"IRQ       : %d\n"
		"PCI Mem   : %#08lx (0x%p)\n"
		"FIFO Mem  : %#08lx (0x%p)\n"
		"Mode      : %s\n"
		"CHIP_ID   : %#02x\n"
		"L1 State  : %c%d\n"
		"Sync Lost : %s\n"
		"Late IRQs : %d\n"
		"FIFO susp : %s\n"
		"\nChannel     %12s %12s %12s %12s %4s %4s %4s\n"
		"D         : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		"B1        : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		"B2        : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		,card->id
		,card->pcidev->irq
		,card->io_bus_mem, card->io_mem
		,(ulong)card->fifo_bus_mem, card->fifo_mem
		,card->nt_mode?"NT":"TE"
		,chip_id
		,card->nt_mode?'G':'F'
		,card->l1_state
		,card->sync_loss_reported?"YES":"NO"
		,card->late_irqs
		,card->fifo_suspended?"YES":"NO"

		,"RX Frames","TX Frames","RX Bytes","TX Bytes","RXFF","TXFF","CRC"
		,card->chans[D].rx.frames
		,card->chans[D].tx.frames
		,card->chans[D].rx.bytes
		,card->chans[D].tx.bytes
		,card->chans[D].rx.fifo_full
		,card->chans[D].tx.fifo_full
		,card->chans[D].rx.crc
		,hfc_status_to_name(card->chans[D].status)

		,card->chans[B1].rx.frames
		,card->chans[B1].tx.frames
		,card->chans[B1].rx.bytes
		,card->chans[B1].tx.bytes
		,card->chans[B1].rx.fifo_full
		,card->chans[B1].tx.fifo_full
		,card->chans[B1].rx.crc
		,hfc_status_to_name(card->chans[B1].status)

		,card->chans[B2].rx.frames
		,card->chans[B2].tx.frames
		,card->chans[B2].rx.bytes
		,card->chans[B2].tx.bytes
		,card->chans[B2].rx.fifo_full
		,card->chans[B2].tx.fifo_full
		,card->chans[B2].rx.crc
		,hfc_status_to_name(card->chans[B2].status)
		);
*/
	return len;
}

static int hfc_proc_read_fifos(char *page, char **start,
		off_t off, int count, 
		int *eof, void *data)
{
	struct hfc_card *card = data;

	int len = 0;
	len += snprintf(page + len, PAGE_SIZE - len,
		"\n      Receive                 Transmit\n"
		"FIFO#  F1 F2   Z1   Z2 Used   F1 F2   Z1   Z2 Used Connected\n");

	int i;
	for (i=0; i<card->num_fifos; i++) {
		unsigned long flags;
		spin_lock_irqsave(&card->lock, flags);

		len += snprintf(page + len, PAGE_SIZE - len,
			"%2d   :", i);

		hfc_fifo_select(&card->fifos[i][RX]);

		union hfc_fgroup f;
		union hfc_zgroup z;

		f.f1f2 = hfc_inw(card, hfc_A_F12);
		z.z1z2 = hfc_inl(card, hfc_A_Z12);

		len += snprintf(page + len, PAGE_SIZE - len,
			" %02x %02x %04x %04x %4d",
			f.f1, f.f2, z.z1, z.z2,
			hfc_fifo_used_rx(&card->fifos[i][RX]));

		hfc_fifo_select(&card->fifos[i][TX]);

		f.f1f2 = hfc_inw(card, hfc_A_F12);
		z.z1z2 = hfc_inl(card, hfc_A_Z12);

		len += snprintf(page + len, PAGE_SIZE - len,
			"   %02x %02x %04x %04x %4d",
			f.f1, f.f2, z.z1, z.z2,
			hfc_fifo_used_tx(&card->fifos[i][TX]));

		if (card->fifos[i][RX].connected_chan) {
			len += snprintf(page + len, PAGE_SIZE - len,
				" %d:%s\n",
				card->fifos[i][RX].connected_chan->chan->port->id,
				card->fifos[i][RX].connected_chan->chan->name);
		} else {
			len += snprintf(page + len, PAGE_SIZE - len, "\n");
		}

		spin_unlock_irqrestore(&card->lock, flags);
	}

	return len;
}

/******************************************
 * Frame interface
 ******************************************/

static int hfc_open(struct visdn_chan *visdn_chan, int mode)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	unsigned long flags;

	// Ok, now the chanel is ours we need to configure it

	if (chan->id != D) {
		WARN_ON(1);
		return -EINVAL;
	}

	spin_lock_irqsave(&card->lock, flags);

	if (chan->status != free) {
		spin_unlock_irqrestore(&card->lock, flags);
		return -EBUSY;
	}

	chan->status = open_hdlc;

	// This is the D channel so let's configure the port first

	hfc_select_port(card, chan->port->id);

	hfc_outb(card, hfc_A_ST_CTRL1, 0);

	// RX
	chan->rx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_IFF|
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	hfc_outb(card, hfc_A_SUBCH_CFG,
		hfc_A_SUBCH_CFG_V_BIT_CNT_2);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_IRQ);

	// TX
	chan->tx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_IFF|
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	hfc_outb(card, hfc_A_SUBCH_CFG,
		hfc_A_SUBCH_CFG_V_BIT_CNT_2);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_IRQ);

	card->open_ports++;

	spin_unlock_irqrestore(&card->lock, flags);

	if (card->open_ports == 1) {
	}

	hfc_msg_chan(KERN_INFO, chan, "channel opened.\n");

	return 0;
}

static int hfc_close(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	unsigned long flags = 0;

	switch (chan->id) {
	case D:
		spin_lock_irqsave(&card->lock, flags);

		if (chan->status != open_hdlc) {
			spin_unlock_irqrestore(&card->lock, flags);
			return -EINVAL;
		}
		chan->status = free;

		hfc_chan_disable(chan);

		spin_unlock_irqrestore(&card->lock, flags);
	break;

	case B1:
	break;

	case B2:
	break;

	case E:
//		BUG();
	break;
	}

	hfc_msg_chan(KERN_INFO, chan, "channel closed.\n");

	return 0;
}

static int hfc_frame_xmit(struct visdn_chan *visdn_chan, struct sk_buff *skb)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	
	hfc_select_port(chan->port->card, chan->port->id);
	hfc_check_l1_up(chan->port);

	hfc_fifo_select(chan->tx.fifo);
	// hfc_fifo_select() updates F/Z cache, so,
	// size calculations are allowed

	if (!hfc_fifo_free_frames(chan->tx.fifo)) {
		hfc_debug_chan(3, chan, "TX FIFO frames full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_frames;
	}

	if (hfc_fifo_free_tx(chan->tx.fifo) < skb->len) {
		hfc_debug_chan(3, chan, "TX FIFO full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_tx;
	}

	hfc_fifo_put_frame(chan->tx.fifo, skb->data, skb->len);

	spin_unlock_irqrestore(&card->lock, flags);

	visdn_kfree_skb(skb);

	return 0;

err_no_free_tx:
err_no_free_frames:

	spin_unlock_irqrestore(&card->lock, flags);

	return 0;
}

static struct net_device_stats *hfc_get_stats(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_card *card = chan->card;

	return &chan->net_device_stats;
}

/*
static void hfc_set_multicast_list(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

        if(netdev->flags & IFF_PROMISC && !port->echo_enabled) {
		if (port->nt_mode) {
			hfc_msg_port(KERN_INFO, port,
				"is in NT mode. Promiscuity is useless\n");

			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		// Only RX FIFO is needed for E channel
		chan->rx.bit_reversed = FALSE;
		hfc_fifo_select(&port->chans[E].rx);
		hfc_fifo_reset(card);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_SUBCH_CFG,
			hfc_A_SUBCH_CFG_V_BIT_CNT_2);

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_IRQ);

		port->echo_enabled = TRUE;

		hfc_msg_port(KERN_INFO, port,
			"entered in promiscuous mode\n");

        } else if(!(netdev->flags & IFF_PROMISC) && port->echo_enabled) {
		if (!port->echo_enabled) {
			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		chan->rx.bit_reversed = FALSE;
		hfc_fifo_select(&port->chans[E].rx);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_IRQ_MSK,
			0);

		port->echo_enabled = FALSE;

		hfc_msg_port(KERN_INFO, port,
			"left promiscuous mode.\n");
	}

	spin_unlock_irqrestore(&card->lock, flags);

//	hfc_update_fifo_state(card);
}
*/

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_timer_interrupt(struct hfc_card *card);
static inline void hfc_handle_state_interrupt(struct hfc_port *port);
static inline void hfc_handle_voice(struct hfc_card *card);
static inline void hfc_handle_port_fifo_interrupt(struct hfc_port *port);

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
		for (i=0; i<card->num_ports; i++) {
			if (irq_oview & (1 << card->ports[i].id)) {
				hfc_handle_port_fifo_interrupt(&card->ports[i]);
			}
		}
	}

	if (irq_sci) {
		int i;
		for (i=0; i<card->num_ports; i++) {
			if (irq_sci & (1 << card->ports[i].id)) {
				hfc_handle_state_interrupt(&card->ports[i]);
			}
		}
	}

	return IRQ_HANDLED;
}

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_chan_simplex *chan)
{
	tasklet_schedule(&chan->tasklet);
}

void hfc_handle_fifo_tx_interrupt(struct hfc_chan_simplex *chan)
{
	if (chan->chan->status == open_hdlc)
		visdn_wake_queue(&chan->chan->visdn_chan);
}

static inline void hfc_handle_port_fifo_interrupt(struct hfc_port *port)
{
	struct hfc_card *card = port->card;

	u8 fifo_irq = hfc_inb(card,
		hfc_R_IRQ_FIFO_BL0 + port->id);

	if (fifo_irq & (1 << 0))
		hfc_handle_fifo_tx_interrupt(&port->chans[B1].tx);
	if (fifo_irq & (1 << 1))
		hfc_handle_fifo_rx_interrupt(&port->chans[B1].rx);
	if (fifo_irq & (1 << 2))
		hfc_handle_fifo_tx_interrupt(&port->chans[B2].tx);
	if (fifo_irq & (1 << 3))
		hfc_handle_fifo_rx_interrupt(&port->chans[B2].rx);
	if (fifo_irq & (1 << 4))
		hfc_handle_fifo_tx_interrupt(&port->chans[D].tx);
	if (fifo_irq & (1 << 5))
		hfc_handle_fifo_rx_interrupt(&port->chans[D].rx);
//	if (fifo_irq & (1 << 6))
//		hfc_handle_fifo_tx_interrupt(&port->chans[E].tx);
	if (fifo_irq & (1 << 7))
		hfc_handle_fifo_rx_interrupt(&port->chans[E].rx);
}

static inline void hfc_handle_timer_interrupt(struct hfc_card *card)
{
	if(card->ignore_first_timer_interrupt) {
		card->ignore_first_timer_interrupt = FALSE;
		return;
	}

/*	if ((port->nt_mode && card->l1_state == 3) ||
		(!card->nt_mode && card->l1_state == 7)) {

		card->regs.ctmt &= ~hfc_CTMT_TIMER_MASK;
		hfc_outb(card, hfc_CTMT, card->regs.ctmt);

		hfc_resume_fifo(card);
	}*/
}

static inline void hfc_handle_state_interrupt(struct hfc_port *port)
{
	struct hfc_card *card = port->card;

	hfc_select_port(card, port->id);

	u8 new_state = hfc_A_ST_RD_STA_V_ST_STA(hfc_inb(card, hfc_A_ST_RD_STA));

	hfc_debug_port(1, port,
		"layer 1 state = %c%d\n",
		port->visdn_port.nt_mode?'G':'F',
		new_state);

	if (port->visdn_port.nt_mode) {
		// NT mode

		if (new_state == 2) {
			// Allows transition from G2 to G3
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
				hfc_A_ST_WR_STA_V_SET_G2_G3);
		} else if (new_state == 3) {
			// fix to G3 state (see specs) ? Why? TODO FIXME
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_SET_STA(3)|
				hfc_A_ST_WR_STA_V_ST_LD_STA);
		}

		if (new_state == 3 && port->l1_state != 3) {
//			hfc_resume_fifo(card);
		}

		if (new_state != 3 && port->l1_state == 3) {
//			hfc_suspend_fifo(card);
		}
	} else {
		if (new_state == 3) {
			if (force_l1_up) {
//				hfc_outb(card, hfc_STATES, hfc_STATES_DO_ACTION |
//						hfc_STATES_ACTIVATE);
			}
		}

		if (new_state == 7 && port->l1_state != 7) {
			// TE is now active, schedule FIFO activation after
			// some time, otherwise the first frames are lost

//			card->regs.ctmt |= hfc_CTMT_TIMER_50 | hfc_CTMT_TIMER_CLEAR;
//			hfc_outb(card, hfc_CTMT, card->regs.ctmt);

			// Activating the timer firest an interrupt immediately, we
			// obviously need to ignore it
		}

		if (new_state != 7 && port->l1_state == 7) {
			// TE has become inactive, disable FIFO
//			hfc_suspend_fifo(card);
		}
	}

	port->l1_state = new_state;
}

static inline void hfc_handle_processing_interrupt(struct hfc_card *card)
{
/*	int available_bytes=0;

	// Synchronize with the first enabled channel
	if(card->regs.fifo_en & hfc_FIFOEN_B1RX)
		available_bytes = hfc_fifo_used_rx(&card->chans[B1].rx);
	if(card->regs.fifo_en & hfc_FIFOEN_B2RX)
		available_bytes = hfc_fifo_used_rx(&card->chans[B2].rx);
	else
		available_bytes = -1;

	if ((available_bytes == -1 && card->ticks == 8) ||
		available_bytes >= CHUNKSIZE + hfc_RX_FIFO_PRELOAD) {
		card->ticks = 0;

		if (available_bytes > CHUNKSIZE + hfc_RX_FIFO_PRELOAD) {
			card->late_irqs++;

			hfc_debug_card(4, 
				"late IRQ, %d bytes late\n",
				available_bytes -
					(CHUNKSIZE +
					 hfc_RX_FIFO_PRELOAD));
		}

		hfc_handle_voice(card);
	}

	card->ticks++;
*/
}

static inline void hfc_handle_voice(struct hfc_card *card)
{
/*	if (card->chans[B1].status != open_trans &&
		card->chans[B2].status != open_trans)
		return;
*/
// TODO
}

/******************************************
 * Module initialization and cleanup
 ******************************************/

static void hfc_config_bchan_for_voice(struct hfc_chan_duplex *chan, int mode)
{
	struct hfc_card *card = chan->port->card;

	u8 st_ctrl_0;
	u8 st_ctrl_2;

	if (chan->id == B1) {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B1_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B1_RX_EN;
	} else {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B2_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B2_RX_EN;
	}

	hfc_debug_chan(0, chan, "configuring channel %s for voice\n", chan->name);

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_select_port(card, chan->port->id);

	if ((mode & O_ACCMODE) == O_RDONLY ||
	    (mode & O_ACCMODE) == O_RDWR) {
		chan->port->regs.st_ctrl_2 |= st_ctrl_2;
		hfc_outb(card, hfc_A_ST_CTRL2, chan->port->regs.st_ctrl_2);
	}

	if ((mode & O_ACCMODE) == O_WRONLY ||
	    (mode & O_ACCMODE) == O_RDWR) {
		chan->port->regs.st_ctrl_0 |= st_ctrl_0;
		hfc_outb(card, hfc_A_ST_CTRL0, chan->port->regs.st_ctrl_0);
	}

	// RX
	chan->rx.fifo->bit_reversed = TRUE;
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	// TX
	chan->tx.fifo->bit_reversed = TRUE;
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	chan->status = open_trans;

	spin_unlock_irqrestore(&card->lock, flags);
}

/*
struct sb_setbearer
{
	int sb_index;
	enum sb_bearertype sb_bearertype;
};

#define VISDN_SET_BEARER	SIOCDEVPRIVATE
#define VISDN_PPP_GET_CHAN	(SIOCDEVPRIVATE+1)
#define VISDN_PPP_GET_UNIT	(SIOCDEVPRIVATE+2)

*/
static int hfc_do_ioctl(struct visdn_chan *visdn_chan,
	struct ifreq *ifr, int cmd)
{
//	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_card *card = chan->port->card;

//	unsigned long flags;
/*

	switch (cmd) {
	case VISDN_SET_BEARER: {

	struct sb_setbearer sb;
	if (copy_from_user(&sb, ifr->ifr_data, sizeof(sb)))
		return -EFAULT;

hfc_msg_chan(KERN_INFO, chan, "VISDN_SET_BEARER %d %d\n", sb.sb_index, sb.sb_bearertype);

	struct hfc_chan_duplex *bchan;
	if (sb.sb_index == 0)
		bchan = &chan->port->chans[B1];
	else if (sb.sb_index == 1)
		bchan = &chan->port->chans[B2];
	else
		return -EINVAL;

	if (sb.sb_bearertype == VISDN_BT_VOICE) {
		
	} else if (sb.sb_bearertype == VISDN_BT_PPP) {
//		hfc_config_bchan_for_ppp(bchan);
	}


	}

	struct hfc_card *card;

	struct hfc_chan_duplex *chan;

	if ((inode->i_rdev - card->first_dev > card->num_ports * 2) ||
	    (inode->i_rdev - card->first_dev < 0))
		return -ENODEV;

	int port_idx = (inode->i_rdev - card->first_dev) / 2;
	u8 st_ctrl_0;
	u8 st_ctrl_2;

	if ((inode->i_rdev - card->first_dev) % 2 == 0) {
		chan = &card->ports[port_idx].chans[B1];

		st_ctrl_0 = hfc_A_ST_CTRL0_V_B1_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B1_RX_EN;
	} else {
		chan = &card->ports[port_idx].chans[B2];

		st_ctrl_0 = hfc_A_ST_CTRL0_V_B2_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B2_RX_EN;
	}

	file->private_data = chan;

	hfc_debug_chan(0, chan, "configuring channel %s for voice\n", chan->name);

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_select_port(card, chan->port->id);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY ||
	    (file->f_flags & O_ACCMODE) == O_RDWR) {
		chan->port->regs.st_ctrl_2 |= st_ctrl_2;
		hfc_outb(card, hfc_A_ST_CTRL2, chan->port->regs.st_ctrl_2);
	}

	if ((file->f_flags & O_ACCMODE) == O_WRONLY ||
	    (file->f_flags & O_ACCMODE) == O_RDWR) {
		chan->port->regs.st_ctrl_0 |= st_ctrl_0;
		hfc_outb(card, hfc_A_ST_CTRL0, chan->port->regs.st_ctrl_0);
	}

	// RX
	chan->rx.bit_reversed = TRUE;
	hfc_fifo_select(&chan->rx);
	hfc_fifo_reset(card);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	// TX
	chan->tx.bit_reversed = TRUE;
	hfc_fifo_select(&chan->tx);
	hfc_fifo_reset(card);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	chan->status = open_trans;

	spin_unlock_irqrestore(&card->lock, flags);
*/

/*

		spin_lock_irqsave(&card->lock, flags);

		hfc_select_port(card, b1_chan->port->id);

		b1_chan->port->regs.st_ctrl_2 |= 
			hfc_A_ST_CTRL2_V_B1_RX_EN;
		hfc_outb(card, hfc_A_ST_CTRL2,
			b1_chan->port->regs.st_ctrl_2);

		b1_chan->port->regs.st_ctrl_0 |=
			hfc_A_ST_CTRL0_V_B1_EN;
		hfc_outb(card, hfc_A_ST_CTRL0,
			b1_chan->port->regs.st_ctrl_0);

		// RX
		chan->rx.bit_reversed = FALSE;
		hfc_fifo_select(&b1_chan->rx);
		hfc_fifo_reset(card);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_IRQ);

		// TX
		chan->tx.bit_reversed = FALSE;
		hfc_fifo_select(&b1_chan->tx);
		hfc_fifo_reset(card);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_IFF|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_IRQ);

		b1_chan->status = open_ppp;

		spin_unlock_irqrestore(&card->lock, flags);

////////////////////////////
		b1_chan->ppp_chan.private = b1_chan;
		b1_chan->ppp_chan.ops = &hfc_ppp_ops;
		b1_chan->ppp_chan.mtu = 1000; //FIXME
		b1_chan->ppp_chan.hdrlen = 2;

		ppp_register_channel(&b1_chan->ppp_chan);
////////////////////////

hfc_msg_chan(KERN_INFO, chan,
	"PPPPPPPPPPPP: int %d unit %d\n",
	ppp_channel_index(&b1_chan->ppp_chan),
	ppp_unit_number(&b1_chan->ppp_chan));


	break;

	case VISDN_PPP_GET_CHAN:
hfc_msg_chan(KERN_INFO, chan, "VISDN_PPP_GET_CHAN:\n");

		put_user(ppp_channel_index(&bchan->ppp_chan),
			(int __user *)ifr->ifr_data);
	break;

	case VISDN_PPP_GET_UNIT:
hfc_msg_chan(KERN_INFO, chan, "VISDN_PPP_GET_UNIT:\n");

		put_user(ppp_unit_number(&bchan->ppp_chan),
			(int __user *)ifr->ifr_data);
	break;

	default:
		return -ENOIOCTLCMD;
	}
*/

	return 0;
}

ssize_t hfc_samples_read(
	struct visdn_chan *visdn_chan,
	char __user *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	int err = 0;

	__u8 buf2[128];

	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_refresh_fz_cache(chan->rx.fifo);

	int available_octets = hfc_fifo_used_rx(chan->rx.fifo);
	int copied_octets = 0;

	if (count > sizeof(buf2))
		count = sizeof(buf2);

	copied_octets = available_octets < count ? available_octets : count;

	hfc_fifo_mem_read(chan->rx.fifo, buf2, copied_octets);

	spin_unlock_irqrestore(&card->lock, flags);

	err = copy_to_user(buf, buf2, copied_octets);
	if (err < 0)
		goto err_copy_to_user;


	return copied_octets;

err_copy_to_user:

	return err;
}

ssize_t hfc_samples_write(
	struct visdn_chan *visdn_chan,
	const char __user *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;
	int err = 0;

	__u8 buf2[256]; // FIXME TODO

	// Umpf... we need an intermediate buffer... we need to disable interrupts
	// for the whole time since we must ensure that noone selects another FIFO
	// in the meantime, so we may not directly copy from FIFO to user.
	// There is for sure a better solution :)

	int copied_octets = count;
	if (copied_octets > sizeof(buf2))
		copied_octets = sizeof(buf2);

	err = copy_from_user(buf2, buf, copied_octets);
	if (err < 0)
		goto err_copy_to_user;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_refresh_fz_cache(chan->tx.fifo);

	int available_octets = hfc_fifo_free_tx(chan->tx.fifo);
	if (copied_octets > available_octets)
		copied_octets = available_octets;

	hfc_fifo_put(chan->rx.fifo, buf2, copied_octets);

	spin_unlock_irqrestore(&card->lock, flags);

	return copied_octets;

err_copy_to_user:

	return err;
}

ssize_t hfc_voice_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	return -EINVAL;
}

int hfc_port_set_role(
	struct visdn_port *visdn_port,
	int nt_mode)
{
	struct hfc_port *port = visdn_port->priv;

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_select_port(port->card, port->id);

	hfc__do_set_role(port, nt_mode);

	spin_unlock_irqrestore(&port->card->lock, flags);

	return 0;
}

static int hfc_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_port *port = to_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_select_port(port->card, port->id);
	hfc_outb(port->card, hfc_A_ST_WR_STA, 0);

	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_port(2, port, "enabled\n");

	return 0;
}

static int hfc_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_port *port = to_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_select_port(port->card, port->id);
	hfc_outb(port->card, hfc_A_ST_WR_STA,
		hfc_A_ST_WR_STA_V_ST_SET_STA(0)|
		hfc_A_ST_WR_STA_V_ST_LD_STA);
	// Should we clear LD_STA?

	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_port(2, port, "disabled\n");

	return 0;
}

static int hfc_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan->connected_chan)
		return -EBUSY;

printk(KERN_INFO "hfc-4s chan %s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	u8 st_ctrl_0;
	u8 st_ctrl_2;

	if (chan->id == B1) {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B1_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B1_RX_EN;
	} else if (chan->id == B2) {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B2_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B2_RX_EN;
	} else {
		hfc_msg(KERN_ERR, "Cannot connect %s to %s\n",
			chan->name, to_chan_duplex(visdn_chan2)->name);
		return -EINVAL;
	}

	hfc_debug_chan(0, chan, "configuring channel %s for voice\n", chan->name);

	unsigned long cpuflags;
	spin_lock_irqsave(&card->lock, cpuflags);

	hfc_select_port(card, chan->port->id);

	// RX
	chan->port->regs.st_ctrl_2 |= st_ctrl_2;
	hfc_outb(card, hfc_A_ST_CTRL2, chan->port->regs.st_ctrl_2);

	// TX
	chan->port->regs.st_ctrl_0 |= st_ctrl_0;
	hfc_outb(card, hfc_A_ST_CTRL0, chan->port->regs.st_ctrl_0);

	// RX
	chan->rx.fifo->bit_reversed = TRUE;
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	// TX
	chan->tx.fifo->bit_reversed = TRUE;
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	chan->status = open_trans;

	spin_unlock_irqrestore(&card->lock, cpuflags);

	return 0;
}

static int hfc_disconnect(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan->connected_chan)
		return 0;

printk(KERN_INFO "hfc-4s chan %s disconnecting from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	u8 st_ctrl_0;
	u8 st_ctrl_2;

	if (chan->id == B1) {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B1_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B1_RX_EN;
	} else {
		st_ctrl_0 = hfc_A_ST_CTRL0_V_B2_EN;
		st_ctrl_2 = hfc_A_ST_CTRL2_V_B2_RX_EN;
	}

	hfc_debug_chan(0, chan, "releasing channel %s for voice\n", chan->name);

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_select_port(card, chan->port->id);

	chan->port->regs.st_ctrl_2 &= ~st_ctrl_2;
	hfc_outb(card, hfc_A_ST_CTRL2, chan->port->regs.st_ctrl_2);

	chan->port->regs.st_ctrl_0 &= ~st_ctrl_0;
	hfc_outb(card, hfc_A_ST_CTRL0, chan->port->regs.st_ctrl_0);

	// RX
	hfc_fifo_select(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	// TX
	hfc_fifo_select(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	spin_unlock_irqrestore(&card->lock, flags);

	return 0;
}

struct visdn_port_ops hfc_port_ops = {
	.set_role	= hfc_port_set_role,
	.enable		= hfc_port_enable,
	.disable	= hfc_port_disable,
};

struct visdn_chan_ops hfc_chan_ops = {
	.open		= hfc_open,
	.close		= hfc_close,
	.frame_xmit	= hfc_frame_xmit,
	.get_stats	= hfc_get_stats,
	.do_ioctl	= hfc_do_ioctl,

	.connect_to	= hfc_connect_to,
	.disconnect	= hfc_disconnect,

	.samples_read	= hfc_samples_read,
	.samples_write	= hfc_samples_write,
};

void hfc_rx_tasklet(unsigned long data)
{
	struct hfc_chan_simplex *chan = (struct hfc_chan_simplex *)data;
	struct hfc_chan_duplex *fdchan = chan->chan;
	struct hfc_card *card = fdchan->port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	// FIFO selection has to be done for each frame to clear
	// internal buffer (see specs 4.4.4).
	hfc_fifo_select(chan->fifo);

	hfc_fifo_refresh_fz_cache(chan->fifo);

	if (!hfc_fifo_has_frames(chan->fifo))
		goto no_frames;

	int frame_size = hfc_fifo_get_frame_size(chan->fifo);

	if (frame_size < 3) {
		hfc_debug_schan(2, chan,
			"invalid frame received, just %d bytes\n",
			frame_size);

		hfc_fifo_drop_frame(chan->fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_invalid_frame;
	} else if(frame_size == 3) {
		hfc_debug_schan(2, chan,
			"empty frame received\n");

		hfc_fifo_drop_frame(chan->fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_empty_frame;
	}

	struct sk_buff *skb =
		visdn_alloc_skb(frame_size - 3);

	if (!skb) {
		hfc_msg_schan(KERN_ERR, chan,
			"cannot allocate skb: frame dropped\n");

		hfc_fifo_drop_frame(chan->fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_alloc_skb;
	}

	if (hfc_fifo_get_frame(chan->fifo,
		skb_put(skb, frame_size - 3),
		frame_size - 3) == -1) {
		visdn_kfree_skb(skb);
		goto err_get_frame;
	}

	fdchan->net_device_stats.rx_packets++;
	fdchan->net_device_stats.rx_bytes += frame_size - 1;

	visdn_frame_rx(&fdchan->visdn_chan, skb);

	goto all_went_well;

err_get_frame:
	kfree_skb(skb);
err_alloc_skb:
err_empty_frame:
err_invalid_frame:
no_frames:
all_went_well:

	if (hfc_fifo_has_frames(chan->fifo))
		tasklet_schedule(&chan->tasklet);

	spin_unlock_irqrestore(&card->lock,flags);
}

static inline void hfc_connect_fifo_chan(
	struct hfc_chan_simplex *chan,
	struct hfc_fifo *fifo)
{
	chan->fifo = fifo;
	fifo->connected_chan = chan;

	hfc_debug_schan(3, chan, "connected to FIFO %d\n",
		fifo->hw_index);
}

static int __devinit hfc_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	static int card_ids_counter = 0;
	int err = 0;

	struct hfc_card *card = NULL;
	card = kmalloc(sizeof(struct hfc_card), GFP_KERNEL);
	if (!card) {
		hfc_msg(KERN_CRIT, "unable to kmalloc!\n");
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	memset(card, 0x00, sizeof(struct hfc_card));

	spin_lock_init(&card->lock);
	card->id = card_ids_counter;
	card->pcidev = pci_dev;

	int i;
	for (i=0; i<sizeof(card->fifos)/sizeof(*card->fifos); i++) {
		card->fifos[i][RX].card = card;
		card->fifos[i][RX].hw_index = i;
		card->fifos[i][RX].direction = RX;

		card->fifos[i][TX].card = card;
		card->fifos[i][TX].hw_index = i;
		card->fifos[i][TX].direction = TX;
	}
	card->num_fifos = 0;

	pci_set_drvdata(pci_dev, card);

	if ((err = pci_enable_device(pci_dev))) {
		goto err_pci_enable_device;
	}

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	if((err = pci_request_regions(pci_dev, hfc_DRIVER_NAME))) {
		hfc_msg_card(KERN_CRIT, card,
			"cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		hfc_msg_card(KERN_CRIT, card,
			"PCI device does not have an assigned IRQ!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev,1);
	if (!card->io_bus_mem) {
		hfc_msg_card(KERN_CRIT, card,
			"PCI device does not have an assigned IO memory area!\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	if(!(card->io_mem = ioremap(card->io_bus_mem, hfc_PCI_MEM_SIZE))) {
		hfc_msg_card(KERN_CRIT, card, "cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	if ((err = request_irq(card->pcidev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card))) {
		hfc_msg_card(KERN_CRIT, card, "unable to register irq\n");
		goto err_request_irq;
	}

	int chip_type = hfc_R_CHIP_ID_V_CHIP_ID(hfc_inb(card, hfc_R_CHIP_ID));

	if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_4S)
		card->num_ports = 4;
	else if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_8S)
		 card->num_ports = 8;
	else {
		hfc_msg_card(KERN_ERR, card,
			"unknown chip type '0x%02x'\n", chip_type);

		goto err_unknown_chip;
	}

	for (i=0; i<card->num_ports; i++) {
		card->ports[i].card = card;
		card->ports[i].id = i;

		visdn_port_init(&card->ports[i].visdn_port, &hfc_port_ops);
		card->ports[i].visdn_port.priv = &card->ports[i];

		struct hfc_chan_duplex *chan;
//---------------------------------- D

		chan = &card->ports[i].chans[D];

		chan->port = &card->ports[i];
		chan->name = "D";
		chan->status = free;
		chan->id = D;

		tasklet_init(&chan->rx.tasklet, hfc_rx_tasklet,
				(unsigned long)&chan->rx);
		chan->rx.chan = chan;
		chan->rx.direction = RX;
		hfc_connect_fifo_chan(&chan->rx,
			&card->fifos[(i * 4) + hfc_SM_D_FIFO_OFF][RX]);

		chan->tx.chan = chan;
		chan->tx.direction = TX;
		hfc_connect_fifo_chan(&chan->tx,
			&card->fifos[(i * 4) + hfc_SM_D_FIFO_OFF][TX]);

		visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
		chan->visdn_chan.priv = chan;
		chan->visdn_chan.speed = 16000;
		chan->visdn_chan.role = VISDN_CHAN_ROLE_D;
		chan->visdn_chan.roles = VISDN_CHAN_ROLE_D;
		chan->visdn_chan.protocol = ETH_P_LAPD;
		chan->visdn_chan.flags = 0;

//---------------------------------- B1
		chan = &card->ports[i].chans[B1];

		chan->port = &card->ports[i];
		chan->name = "B1";
		chan->status = free;
		chan->id = B1;

		tasklet_init(&chan->rx.tasklet, hfc_rx_tasklet,
				(unsigned long)&chan->rx);
		chan->rx.chan = chan;
		chan->rx.direction = RX;
		hfc_connect_fifo_chan(&chan->rx,
			&card->fifos[(i * 4) + hfc_SM_B1_FIFO_OFF][RX]);

		chan->tx.chan = chan;
		chan->tx.direction = TX;
		hfc_connect_fifo_chan(&chan->tx,
			&card->fifos[(i * 4) + hfc_SM_B1_FIFO_OFF][TX]);

		visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
		chan->visdn_chan.priv = chan;
		chan->visdn_chan.speed = 64000;
		chan->visdn_chan.role = VISDN_CHAN_ROLE_B;
		chan->visdn_chan.roles = VISDN_CHAN_ROLE_B;
		chan->visdn_chan.protocol = 0;
		chan->visdn_chan.flags = 0;

//---------------------------------- B2
		chan = &card->ports[i].chans[B2];

		chan->port = &card->ports[i];
		chan->name = "B2";
		chan->status = free;
		chan->id = B2;


		tasklet_init(&chan->rx.tasklet, hfc_rx_tasklet,
				(unsigned long)&chan->rx);
		chan->rx.chan = chan;
		chan->rx.direction = RX;
		hfc_connect_fifo_chan(&chan->rx,
			&card->fifos[(i * 4) + hfc_SM_B2_FIFO_OFF][RX]);

		chan->tx.chan = chan;
		chan->tx.direction = TX;
		hfc_connect_fifo_chan(&chan->tx,
			&card->fifos[(i * 4) + hfc_SM_B2_FIFO_OFF][TX]);

		visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
		chan->visdn_chan.priv = chan;
		chan->visdn_chan.speed = 64000;
		chan->visdn_chan.role = VISDN_CHAN_ROLE_B;
		chan->visdn_chan.roles = VISDN_CHAN_ROLE_B;
		chan->visdn_chan.protocol = 0;
		chan->visdn_chan.flags = 0;

//---------------------------------- E
		chan = &card->ports[i].chans[E];

		chan->port = &card->ports[i];
		chan->name = "E";
		chan->status = free;
		chan->id = E;

		tasklet_init(&chan->rx.tasklet, hfc_rx_tasklet,
				(unsigned long)&chan->rx);
		chan->rx.chan = chan;
		chan->rx.direction = RX;
		hfc_connect_fifo_chan(&chan->rx,
			&card->fifos[(i * 4) + hfc_SM_E_FIFO_OFF][RX]);

		chan->tx.chan = NULL;
		chan->tx.direction = TX;

		visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
		chan->visdn_chan.role = VISDN_CHAN_ROLE_E;
		chan->visdn_chan.roles = VISDN_CHAN_ROLE_E;
		chan->visdn_chan.flags = 0;
		chan->visdn_chan.protocol = 0;
		chan->visdn_chan.priv = chan;

//---------------------------------- E
		chan = &card->ports[i].chans[SQ];

		chan->port = &card->ports[i];
		chan->name = "SQ";
		chan->status = free;
		chan->id = SQ;

		tasklet_init(&chan->rx.tasklet, hfc_rx_tasklet,
				(unsigned long)&chan->rx);
		chan->rx.chan = chan;
		chan->rx.direction = RX;
		hfc_connect_fifo_chan(&chan->rx,
			&card->fifos[(i * 4) + hfc_SM_E_FIFO_OFF][RX]);

		chan->tx.chan = NULL;
		chan->tx.direction = TX;

		visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
		chan->visdn_chan.role = VISDN_CHAN_ROLE_E;
		chan->visdn_chan.roles = VISDN_CHAN_ROLE_E;
		chan->visdn_chan.flags = 0;
		chan->visdn_chan.protocol = 0;
		chan->visdn_chan.priv = chan;
	}

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	hfc_initialize_hw_nonsoft(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	spin_unlock_irqrestore(&card->lock, flags);

	// Ok, the hardware is ready and the data structures are initialized,
	// we can now register to the system.

	for (i=0; i<card->num_ports; i++) {
		char portid[10];
		snprintf(portid, sizeof(portid), "st%d", i);

		visdn_port_register(&card->ports[i].visdn_port,
			portid, &pci_dev->dev);

		struct hfc_chan_duplex *chan;

		visdn_chan_register(&card->ports[i].chans[D].visdn_chan, "D",
			&card->ports[i].visdn_port);

		err = device_create_file(
			&card->ports[i].chans[D].visdn_chan.device,
			&dev_attr_bert_enabled);
		if (err < 0) {
			/*FIXME*/
		}

		visdn_chan_register(&card->ports[i].chans[B1].visdn_chan, "B1",
			&card->ports[i].visdn_port);

		err = device_create_file(
			&card->ports[i].chans[B1].visdn_chan.device,
			&dev_attr_bert_enabled);
		if (err < 0) {
			/*FIXME*/
		}

		visdn_chan_register(&card->ports[i].chans[B2].visdn_chan, "B2",
			&card->ports[i].visdn_port);

		err = device_create_file(
			&card->ports[i].chans[B2].visdn_chan.device,
			&dev_attr_bert_enabled);
		if (err < 0) {
			/*FIXME*/
		}

		visdn_chan_register(&card->ports[i].chans[E].visdn_chan, "E",
			&card->ports[i].visdn_port);

		err = device_create_file(
			&card->ports[i].visdn_port.device,
			&dev_attr_l1_state);
		if (err < 0) {
			/*FIXME*/
		}

		err = device_create_file(
			&card->ports[i].visdn_port.device,
			&dev_attr_st_clock_delay);
		if (err < 0) {
			/*FIXME*/
		}
		err = device_create_file(
			&card->ports[i].visdn_port.device,
			&dev_attr_st_sampling_comp);
		if (err < 0) {
			/*FIXME*/
		}
	}

// -------------------------------------------------------

	snprintf(card->proc_dir_name,
			sizeof(card->proc_dir_name),
			"%d", card->id);
	card->proc_dir = proc_mkdir(card->proc_dir_name, hfc_proc_hfc_dir);
	card->proc_dir->owner = THIS_MODULE;

	card->proc_info = create_proc_read_entry(
			"info", 0444, card->proc_dir,
			hfc_proc_read_info, card);
	card->proc_info->owner = THIS_MODULE;

	card->proc_fifos = create_proc_read_entry(
			"fifos", 0400, card->proc_dir,
			hfc_proc_read_fifos, card);
	card->proc_fifos->owner = THIS_MODULE;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_clock_source_config);
	if (err < 0)
		goto err_device_create_file_clock_source_config;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_clock_source_current);
	if (err < 0)
		goto err_device_create_file_clock_source_current;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_dip_switches);
	if (err < 0)
		goto err_device_create_file_dip_switches;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_ramsize);
	if (err < 0)
		goto err_device_create_file_ramsize;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_bert_mode);
	if (err < 0)
		goto err_device_create_file_bert_mode;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_bert_err);
	if (err < 0)
		goto err_device_create_file_bert_err;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_bert_sync);
	if (err < 0)
		goto err_device_create_file_bert_sync;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_bert_inv);
	if (err < 0)
		goto err_device_create_file_bert_inv;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_bert_cnt);
	if (err < 0)
		goto err_device_create_file_bert_cnt;

	err = device_create_file(
		&pci_dev->dev,
		&dev_attr_output_level);
	if (err < 0)
		goto err_device_create_file_output_level;

	hfc_msg_card(KERN_INFO, card,
		"configured at mem %#lx (0x%p) IRQ %u\n",
		card->io_bus_mem,
		card->io_mem,
		card->pcidev->irq); 

	card_ids_counter++;

	return 0;

	visdn_port_unregister(&card->ports[3].visdn_port);
	visdn_port_unregister(&card->ports[2].visdn_port);
	visdn_port_unregister(&card->ports[1].visdn_port);
	visdn_port_unregister(&card->ports[0].visdn_port);

	device_remove_file(&pci_dev->dev, &dev_attr_output_level);
err_device_create_file_output_level:
	device_remove_file(&pci_dev->dev, &dev_attr_bert_cnt);
err_device_create_file_bert_cnt:
	device_remove_file(&pci_dev->dev, &dev_attr_bert_inv);
err_device_create_file_bert_inv:
	device_remove_file(&pci_dev->dev, &dev_attr_bert_sync);
err_device_create_file_bert_sync:
	device_remove_file(&pci_dev->dev, &dev_attr_bert_err);
err_device_create_file_bert_err:
	device_remove_file(&pci_dev->dev, &dev_attr_bert_mode);
err_device_create_file_bert_mode:
	device_remove_file(&pci_dev->dev, &dev_attr_ramsize);
err_device_create_file_ramsize:
	device_remove_file(&pci_dev->dev, &dev_attr_dip_switches);
err_device_create_file_dip_switches:
	device_remove_file(&pci_dev->dev, &dev_attr_clock_source_current);
err_device_create_file_clock_source_current:
	device_remove_file(&pci_dev->dev, &dev_attr_clock_source_config);
err_device_create_file_clock_source_config:
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

	hfc_msg_card(KERN_INFO, card,
		"shutting down card at %p.\n",
		card->io_mem);

	// Unregister the card from the system
	device_remove_file(&pci_dev->dev, &dev_attr_output_level);

	device_remove_file(&pci_dev->dev, &dev_attr_bert_cnt);
	device_remove_file(&pci_dev->dev, &dev_attr_bert_inv);
	device_remove_file(&pci_dev->dev, &dev_attr_bert_sync);
	device_remove_file(&pci_dev->dev, &dev_attr_bert_err);
	device_remove_file(&pci_dev->dev, &dev_attr_bert_mode);

	device_remove_file(&pci_dev->dev, &dev_attr_ramsize);
	device_remove_file(&pci_dev->dev, &dev_attr_dip_switches);
	device_remove_file(&pci_dev->dev, &dev_attr_clock_source_current);
	device_remove_file(&pci_dev->dev, &dev_attr_clock_source_config);

	visdn_chan_unregister(&card->ports[3].chans[E].visdn_chan);
	visdn_chan_unregister(&card->ports[3].chans[B2].visdn_chan);
	visdn_chan_unregister(&card->ports[3].chans[B1].visdn_chan);
	visdn_chan_unregister(&card->ports[3].chans[D].visdn_chan);
	visdn_port_unregister(&card->ports[3].visdn_port);

	visdn_chan_unregister(&card->ports[2].chans[E].visdn_chan);
	visdn_chan_unregister(&card->ports[2].chans[B2].visdn_chan);
	visdn_chan_unregister(&card->ports[2].chans[B1].visdn_chan);
	visdn_chan_unregister(&card->ports[2].chans[D].visdn_chan);
	visdn_port_unregister(&card->ports[2].visdn_port);

	visdn_chan_unregister(&card->ports[1].chans[E].visdn_chan);
	visdn_chan_unregister(&card->ports[1].chans[B2].visdn_chan);
	visdn_chan_unregister(&card->ports[1].chans[B1].visdn_chan);
	visdn_chan_unregister(&card->ports[1].chans[D].visdn_chan);
	visdn_port_unregister(&card->ports[1].visdn_port);

	visdn_chan_unregister(&card->ports[0].chans[E].visdn_chan);
	visdn_chan_unregister(&card->ports[0].chans[B2].visdn_chan);
	visdn_chan_unregister(&card->ports[0].chans[B1].visdn_chan);
	visdn_chan_unregister(&card->ports[0].chans[D].visdn_chan);
	visdn_port_unregister(&card->ports[0].visdn_port);

	remove_proc_entry("fifos", card->proc_dir);
	remove_proc_entry("info", card->proc_dir);
	remove_proc_entry(card->proc_dir_name, hfc_proc_hfc_dir);

	// softreset clears all pending interrupts
	unsigned long flags;
	spin_lock_irqsave(&card->lock,flags);
	hfc_softreset(card);
	spin_unlock_irqrestore(&card->lock,flags);

	// There should be no interrupt from here on

	pci_write_config_word(pci_dev, PCI_COMMAND, 0);
	free_irq(pci_dev->irq, card);
	iounmap(card->io_mem);
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
	kfree(card);
}

/******************************************
 * Module stuff
 ******************************************/

static int __init hfc_init_module(void)
{
	int ret;

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " loading\n");

	hfc_proc_hfc_dir = proc_mkdir(hfc_DRIVER_NAME, proc_root_driver);

	ret = pci_module_init(&hfc_driver);

	return ret;
}

module_init(hfc_init_module);

static void __exit hfc_module_exit(void)
{
	pci_unregister_driver(&hfc_driver);

	remove_proc_entry(hfc_DRIVER_NAME, proc_root_driver);

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " unloaded\n");
}

module_exit(hfc_module_exit);

#endif

MODULE_DESCRIPTION(hfc_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#ifdef LINUX26

module_param(force_l1_up, int, 0444);
#ifdef DEBUG
module_param(debug_level, int, 0444);
#endif

#else

MODULE_PARM(force_l1_up,"i");
#ifdef DEBUG
MODULE_PARM(debug_level,"i");
#endif

#endif // LINUX26

MODULE_PARM_DESC(force_l1_up, "Don't allow L1 to go down");

#ifdef DEBUG
MODULE_PARM_DESC(debug_level, "List of card IDs to configure in NT mode");
#endif



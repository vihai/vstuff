/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
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

#include <linux/visdn/core.h>

#include "fifo.h"
#include "fifo_inline.h"
#include "led.h"
#include "e1_port.h"
#include "e1_port_inline.h"
#include "pcm_port.h"
#include "pcm_port_inline.h"
#include "e1_port.h"
#include "e1_chan.h"
#include "sys_port.h"
#include "sys_chan.h"
#include "card.h"
#include "card_inline.h"

//----------------------------------------------------------------------------

static ssize_t hfc_show_double_clock(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->double_clock ? 1 : 0);
}

static ssize_t hfc_store_double_clock(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	card->double_clock = !!value;
	card->quartz_65 = !!value;
	hfc_update_r_ctrl(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(double_clock, S_IRUGO | S_IWUSR,
		hfc_show_double_clock,
		hfc_store_double_clock);

//----------------------------------------------------------------------------

static ssize_t hfc_show_quartz_65(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->quartz_65 ? 1 : 0);
}

static ssize_t hfc_store_quartz_65(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	card->quartz_65 = !!value;
	hfc_update_r_brg_pcm_cfg(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(quartz_65, S_IRUGO | S_IWUSR,
		hfc_show_quartz_65,
		hfc_store_quartz_65);

//----------------------------------------------------------------------------

static ssize_t hfc_show_output_level(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n", card->output_level);
}

static ssize_t hfc_store_output_level(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int value;
	if (sscanf(buf, "%x", &value) < 1)
		return -EINVAL;

	if (value < 0 || value > 0xff)
		return -EINVAL;

	// Uhm... this should be safe without locks, indagate
	hfc_card_lock(card);
	hfc_outb(card, hfc_R_PWM0, value);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(output_level, S_IRUGO | S_IWUSR,
		hfc_show_output_level,
		hfc_store_output_level);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_mode(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->bert_mode);
}

static ssize_t hfc_store_bert_mode(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int mode;
	sscanf(buf, "%d", &mode);

	hfc_card_lock(card);
	card->bert_mode = mode;
	hfc_update_bert_wd_md(card, 0);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(bert_mode, S_IRUGO | S_IWUSR,
		hfc_show_bert_mode,
		hfc_store_bert_mode);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_err(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	return -ENOTSUPP;
}

static ssize_t hfc_store_bert_err(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	hfc_card_lock(card);
	hfc_update_bert_wd_md(card, hfc_R_BERT_WD_MD_V_BERT_ERR);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(bert_err, S_IWUSR,
		hfc_show_bert_err,
		hfc_store_bert_err);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_sync(
	struct device *device,
	DEVICE_ATTR_COMPAT
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
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_inv(
	struct device *device,
	DEVICE_ATTR_COMPAT
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
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_bert_cnt(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int cnt;
	hfc_card_lock(card);
	cnt = hfc_inb(card, hfc_R_BERT_ECL);
	cnt += hfc_inb(card, hfc_R_BERT_ECH) << 8;
	hfc_card_unlock(card);

	return snprintf(buf, PAGE_SIZE, "%d\n", cnt);
}

static DEVICE_ATTR(bert_cnt, S_IRUGO,
		hfc_show_bert_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_ram_bandwidth(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d\n",
		(hfc_inb(card, hfc_R_RAM_USE) * 100) / 0x7c);
}

static DEVICE_ATTR(ram_bandwidth, S_IRUGO,
		hfc_show_ram_bandwidth,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_ram_size(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d\n",
		card->ram_size);
}

static ssize_t hfc_store_ram_size(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	unsigned int value;

	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	if (value != 32 &&
	    value != 128 &&
	    value != 512)
		return -EINVAL;

	if (value != card->ram_size) {
		hfc_card_lock(card);

		if (value == 32)
			hfc_sys_port_reconfigure(&card->sys_port, 0, 0, 0);
		else if (value == 128)
			hfc_sys_port_reconfigure(&card->sys_port, 1, 0, 0);
		else if (value == 512)
			hfc_sys_port_reconfigure(&card->sys_port, 2, 0, 0);

		card->ram_size = value;

		hfc_update_r_ram_misc(card);
		hfc_update_r_ctrl(card);

		hfc_softreset(card);
		hfc_initialize_hw(card);

		hfc_card_unlock(card);

		hfc_debug_card(card, 1, "RAM size set to %d\n", value);
	}

	return count;
}

static DEVICE_ATTR(ram_size, S_IRUGO | S_IWUSR,
		hfc_show_ram_size,
		hfc_store_ram_size);

//----------------------------------------------------------------------------

static ssize_t hfc_show_dip_switches(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n",
		(~hfc_inb(card, hfc_R_GPIO_IN1) >> 5) & 0x7);
}

static DEVICE_ATTR(dip_switches, S_IRUGO,
		hfc_show_dip_switches,
		NULL);

static struct device_attribute *hfc_card_attributes[] =
{
	&dev_attr_double_clock,
	&dev_attr_quartz_65,
	&dev_attr_dip_switches,
	&dev_attr_ram_bandwidth,
	&dev_attr_ram_size,
	&dev_attr_bert_mode,
	&dev_attr_bert_err,
	&dev_attr_bert_sync,
	&dev_attr_bert_inv,
	&dev_attr_bert_cnt,
	&dev_attr_output_level,
	NULL
};

int hfc_card_sysfs_create_files(
	struct hfc_card *card)
{
	struct device_attribute **attr = hfc_card_attributes;

	while(*attr) {
		device_create_file(
			&card->pci_dev->dev,
			*attr);

		attr++;
	}

	return 0;
}

void hfc_card_sysfs_delete_files(
	struct hfc_card *card)
{
	struct device_attribute **attr = hfc_card_attributes;

	while(*attr) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr++;
	}
}

/******************************************
 * HW routines
 ******************************************/

void hfc_softreset(struct hfc_card *card)
{
	hfc_msg_card(card, KERN_INFO, "resetting\n");

	mb();
	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_SRES);
	mb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();

	hfc_wait_busy(card);
}

void hfc_update_r_ctrl(struct hfc_card *card)
{
	u8 r_ctrl = 0;

	if (card->double_clock)
		r_ctrl |= hfc_R_CTRL_V_ST_CLK_DIV_8;
	else
		r_ctrl |= hfc_R_CTRL_V_ST_CLK_DIV_4;

	if (card->ram_size == 128 ||
	    card->ram_size == 512)
		r_ctrl |= hfc_R_CTRL_V_EXT_RAM;

	hfc_outb(card, hfc_R_CTRL, r_ctrl);
}

void hfc_update_r_brg_pcm_cfg(struct hfc_card *card)
{
	if (card->quartz_65)
		hfc_outb(card, hfc_R_BRG_PCM_CFG,
			hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV4);
	else
		hfc_outb(card, hfc_R_BRG_PCM_CFG,
			hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV2);
}

void hfc_update_r_ram_misc(struct hfc_card *card)
{
	u8 ram_misc = hfc_R_RAM_MISC_V_FZ_MD;

	if (card->ram_size == 32)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_32K;
	else if (card->ram_size == 128)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_128K;
	else if (card->ram_size == 512)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_512K;

	hfc_outb(card, hfc_R_RAM_MISC, ram_misc);
}

static void hfc_initialize_hw_nonsoft(struct hfc_card *card)
{
	// FIFO RAM configuration
	hfc_update_r_ram_misc(card);

	hfc_outb(card, hfc_R_FIFO_MD,
			hfc_R_FIFO_MD_V_FIFO_MD_00 |
			hfc_R_FIFO_MD_V_DF_MD_FSM |
			hfc_R_FIFO_MD_V_FIFO_SZ_00);

	hfc_sys_port_configure(&card->sys_port, 0, 0, 0);

	// IRQ MISC config
	hfc_outb(card, hfc_R_IRQMSK_MISC, 0);

	hfc_update_r_ctrl(card);
	hfc_update_r_brg_pcm_cfg(card);

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
	u8 pcm_md1 = 0;
	if (card->pcm_port.bitrate == 0) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_2MBIT;
	} else if (card->pcm_port.bitrate == 1) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_4MBIT;
	} else if (card->pcm_port.bitrate == 2) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_8MBIT;
	}

	hfc_pcm_multireg_select(card, hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD1);
	hfc_outb(card, hfc_R_PCM_MD1, pcm_md1);
}

void hfc_update_bert_wd_md(struct hfc_card *card, u8 otherbits)
{
	hfc_outb(card, hfc_R_BERT_WD_MD,
		hfc_R_BERT_WD_MD_V_PAT_SEQ(card->bert_mode & 0x7) |
		otherbits);
}

void hfc_initialize_hw(struct hfc_card *card)
{
	card->output_level = 0x50;
	card->bert_mode = 0;

	card->pcm_port.master = TRUE;
	card->pcm_port.bitrate = 0;
	card->pcm_port.num_chans = 0;

	/* Ensure the state machine is locked down */
	hfc_outb(card, hfc_R_E1_WR_STA,
		hfc_R_E1_WR_STA_V_E1_SET_STA(0)|
		hfc_R_E1_WR_STA_V_E1_LD_STA);

	hfc_outb(card, hfc_R_PWM_MD,
		hfc_R_PWM_MD_V_PWM0_MD_PUSH |
		hfc_R_PWM_MD_V_PWM1_MD_PUSH);

	hfc_outb(card, hfc_R_PWM0, card->output_level);
	hfc_outb(card, hfc_R_PWM1, 0xff); // CHECKME XXX

	hfc_outb(card, hfc_R_TI_WD,
		hfc_R_TI_WD_V_EV_TS_8_192_S |
		hfc_R_TI_WD_V_WD_TS_65_536_S);

	hfc_outb(card, hfc_R_LOS0, 0x7f);
	hfc_outb(card, hfc_R_LOS1, 0x7f);

	card->e1_port.rx_line_code = hfc_LINE_CODE_HDB3;
	card->e1_port.rx_full_baud = FALSE;
	hfc_e1_port_update_rx0(&card->e1_port);

	card->e1_port.tx_line_code = hfc_LINE_CODE_HDB3;
	card->e1_port.tx_full_baud = FALSE;
	hfc_e1_port_update_tx0(&card->e1_port);

	hfc_outb(card, hfc_R_RX_SL0_CFG0,
		hfc_R_RX_SL0_CFG0_V_AUTO_RESYNC |
		hfc_R_RX_SL0_CFG0_V_AUTO_RECO);

	card->e1_port.rx_crc4 = TRUE;
	hfc_e1_port_update_rx_sl0_cfg1(&card->e1_port);

	hfc_outb(card, hfc_R_TX1,
		hfc_R_TX1_V_ATX |
		hfc_R_TX1_V_NTRI |
		hfc_R_TX1_V_AUTO_ERR_RES);

	hfc_outb(card, hfc_R_TX_SL0_CFG0, 0);
	hfc_outb(card, hfc_R_TX_SL0, 0);

	card->e1_port.tx_crc4 = TRUE;
	hfc_e1_port_update_tx_sl0_cfg1(&card->e1_port);

	hfc_outb(card, hfc_R_JATT_CFG,
		hfc_R_JATT_CFG_V_JATT_FRQ_IV_128MS | 0x90);

	hfc_outb(card, hfc_R_RX_OFFS,
		hfc_R_RX_OFFS_V_RX_OFFS(2) |
		hfc_R_RX_OFFS_V_RX_INIT);

	hfc_outb(card, hfc_R_SYNC_OUT, 0);

	hfc_outb(card, hfc_R_TX_OFFS,
		hfc_R_TX_OFFS_V_TX_OFFS(2) |
		hfc_R_TX_OFFS_V_TX_INIT);

	hfc_outb(card, hfc_R_SYNC_CTRL,
		hfc_R_SYNC_CTRL_V_JATT_MAN);


	hfc_update_pcm_md0(card, 0);
	hfc_update_pcm_md1(card);
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

	hfc_outb(card, hfc_R_GPIO_SEL,
		hfc_R_GPIO_SEL_V_GPIO_SEL4 |
		hfc_R_GPIO_SEL_V_GPIO_SEL5);

	hfc_outb(card, hfc_R_GPIO_EN1,
		hfc_R_GPIO_EN1_V_GPIO_EN8 |
		hfc_R_GPIO_EN1_V_GPIO_EN9 |
		hfc_R_GPIO_EN1_V_GPIO_EN10 |
		hfc_R_GPIO_EN1_V_GPIO_EN11);

	card->gpio_out = 
		hfc_R_GPIO_OUT1_V_GPIO_OUT8 |
		hfc_R_GPIO_OUT1_V_GPIO_OUT9 |
		hfc_R_GPIO_OUT1_V_GPIO_OUT10 |
		hfc_R_GPIO_OUT1_V_GPIO_OUT11;
	hfc_outb(card, hfc_R_GPIO_OUT1, card->gpio_out);

	// Timer interrupt enabled
	hfc_outb(card, hfc_R_IRQMSK_MISC,
		hfc_R_IRQMSK_MISC_V_TI_IRQMSK |
		hfc_R_IRQMSK_MISC_V_IRQ1S_MSK);

	/* Initialize E1 port
	hfc_e1_port_update_e1_ctrl_0(&card->e1_ports[i]);
	hfc_e1_port_update_e1_ctrl_2(&card->e1_ports[i]);
	*/

	// Enable interrupts
	hfc_outb(card, hfc_R_IRQ_CTRL,
		hfc_R_IRQ_CTRL_V_FIFO_IRQ|
		hfc_R_IRQ_CTRL_V_GLOB_IRQ_EN|
		hfc_R_IRQ_CTRL_V_IRQ_POL_LOW);
}

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_fifo_tx_interrupt(struct hfc_sys_chan *chan)
{
	if (visdn_leg_queue_stopped(&chan->visdn_chan.leg_b)) {
		hfc_fifo_select(&chan->tx_fifo);

		if (hfc_fifo_free_frames(&chan->tx_fifo) &&
		    hfc_fifo_free_tx(&chan->tx_fifo) > 20)
			visdn_leg_wake_queue(&chan->visdn_chan.leg_b);
	}
}

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_sys_chan *chan)
{
	schedule_work(&chan->rx_work);
}

static inline void hfc_handle_timer_interrupt(struct hfc_card *card)
{
}

static inline void hfc_handle_state_interrupt(struct hfc_e1_port *port)
{
	schedule_work(&port->state_change_work);
}

static inline void hfc_handle_fifo_block_interrupt(
	struct hfc_card *card, int block)
{
	u8 fifo_irq = hfc_inb(card,
		hfc_R_IRQ_FIFO_BL0 + block);

	if (fifo_irq & (1 << 0))
		hfc_handle_fifo_tx_interrupt(
			&card->sys_port.chans[block * 4 + 0]);

	if (fifo_irq & (1 << 1))
		hfc_handle_fifo_rx_interrupt(
			&card->sys_port.chans[block * 4 + 0]);

	if (fifo_irq & (1 << 2))
		hfc_handle_fifo_tx_interrupt(
			&card->sys_port.chans[block * 4 + 1]);

	if (fifo_irq & (1 << 3))
		hfc_handle_fifo_rx_interrupt(
			&card->sys_port.chans[block * 4 + 1]);

	if (fifo_irq & (1 << 4))
		hfc_handle_fifo_tx_interrupt(
			&card->sys_port.chans[block * 4 + 2]);

	if (fifo_irq & (1 << 5))
		hfc_handle_fifo_rx_interrupt(
			&card->sys_port.chans[block * 4 + 2]);

	if (fifo_irq & (1 << 6))
		hfc_handle_fifo_tx_interrupt(
			&card->sys_port.chans[block * 4 + 3]);

	if (fifo_irq & (1 << 7))
		hfc_handle_fifo_rx_interrupt(
			&card->sys_port.chans[block * 4 + 3]);
}

/*
 * Interrupt handling routine.
 *
 * NOTE: We must not change fifo/port/slot selection registers or else
 * we will race with other code. Using spin_lock_irq instead of spin_lock
 * elsewere is not a possibility.
 *
 */

static irqreturn_t hfc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct hfc_card *card = dev_id;

	u8 status = hfc_inb(card, hfc_R_STATUS);
	if (unlikely(
		!(status & (hfc_R_STATUS_V_MISC_IRQSTA |
		            hfc_R_STATUS_V_FR_IRQSTA)))) {
		// probably we are sharing the irq
		return IRQ_NONE;
	}

	if (status & hfc_R_STATUS_V_MISC_IRQSTA) {
		u8 irq_misc = hfc_inb(card, hfc_R_IRQ_MISC);

		if (irq_misc & hfc_R_IRQ_MISC_V_TI_IRQ)
			hfc_handle_timer_interrupt(card);

		if (irq_misc & hfc_R_IRQ_MISC_V_STA_IRQ)
			hfc_handle_state_interrupt(&card->e1_port);

		if (irq_misc & hfc_R_IRQ_MISC_V_IRQ1S)
			schedule_work(&card->e1_port.counters_update_work);
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

	return IRQ_HANDLED;
}

int __devinit hfc_card_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry)
{
	struct hfc_card *card;
	struct hfc_card_config *card_config;
	int chip_type;
	int revision;
	int i;
	int err;

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

	card_config = (struct hfc_card_config *)device_id_entry->driver_data;

	card->double_clock = card_config->double_clock;
	card->quartz_65 = card_config->quartz_65;
	card->ram_size = card_config->ram_size;

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		hfc_led_init(&card->leds[i], i, card);

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
			"PCI device does not have an assigned"
			" IO memory area!\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	card->io_mem = ioremap(card->io_bus_mem, hfc_PCI_MEM_SIZE);
	if(!card->io_mem) {
		hfc_msg_card(card, KERN_CRIT, "cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}

	err = request_irq(card->pci_dev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card);
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT, "unable to register irq\n");
		goto err_request_irq;
	}

	chip_type = hfc_R_CHIP_ID_V_CHIP_ID(hfc_inb(card, hfc_R_CHIP_ID));

	if (chip_type != hfc_R_CHIP_ID_V_CHIP_ID_HFC_E1) {
		hfc_msg_card(card, KERN_ERR,
			"unknown chip type '0x%02x'\n", chip_type);

		goto err_unknown_chip;
	}

	revision = hfc_R_CHIP_RV_V_CHIP_RV(hfc_inb(card, hfc_R_CHIP_RV));

	if (revision < 1)
		goto err_unsupported_revision;

	hfc_msg_card(card, KERN_ERR,
		"HFC-E1 chip rev. %02x detected, "
		"using quartz=%s MHz, doubleclock=%d\n",
			revision,
			card->quartz_65 ? "65,536" : "32,768",
			card->double_clock ? 1 : 0);

	// Initialize all the card's components

	hfc_cxc_init(&card->cxc);

	hfc_e1_port_init(&card->e1_port, card, "e1");

	hfc_sys_port_init(&card->sys_port, card, "sys");
	hfc_pcm_port_init(&card->pcm_port, card, "pcm");

	hfc_card_lock(card);
	hfc_initialize_hw_nonsoft(card);
	hfc_softreset(card);
	hfc_initialize_hw(card);
	hfc_card_unlock(card);

	// Ok, the hardware is ready and the data structures are initialized,
	// we can now register to the system.

	err = visdn_cxc_register(&card->cxc.visdn_cxc);
	if (err < 0)
		goto err_cxc_register;

	err = hfc_sys_port_register(&card->sys_port);
	if (err < 0)
		goto err_sys_port_register;

	err = hfc_pcm_port_register(&card->pcm_port);
	if (err < 0)
		goto err_pcm_port_register;

	err = hfc_e1_port_register(&card->e1_port);
	if (err < 0)
		goto err_e1_port_register;

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
	hfc_e1_port_unregister(&card->e1_port);
err_e1_port_register:
	hfc_pcm_port_unregister(&card->pcm_port);
err_pcm_port_register:
	hfc_sys_port_unregister(&card->sys_port);
err_sys_port_register:
	visdn_cxc_unregister(&card->cxc.visdn_cxc);
err_cxc_register:
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
	pci_set_drvdata(pci_dev, NULL);
	kfree(card);
err_alloc_hfccard:

	hfc_msg(KERN_ERR, "HFC card at %s-%s initialization failure: %d\n",
		pci_dev->dev.bus->name,
		pci_dev->dev.bus_id,
		err);

	return err;
}

void __devexit hfc_card_remove(struct hfc_card *card)
{
	int i;

	hfc_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n",
		card->io_mem);

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		hfc_led_remove(&card->leds[i]);

	hfc_card_sysfs_delete_files(card);

	hfc_e1_port_unregister(&card->e1_port);

	hfc_pcm_port_unregister(&card->pcm_port);
	hfc_sys_port_unregister(&card->sys_port);
	visdn_cxc_unregister(&card->cxc.visdn_cxc);

	// softreset clears all pending interrupts
	hfc_card_lock(card);
	hfc_softreset(card);
	hfc_card_unlock(card);

	// There should be no interrupt from here on

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);
	free_irq(card->pci_dev->irq, card);
	iounmap(card->io_mem);
	pci_release_regions(card->pci_dev);
	pci_disable_device(card->pci_dev);
	kfree(card);
}

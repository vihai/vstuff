/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/autoconf.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/softswitch.h>

#include "fifo.h"
#include "fifo_inline.h"
#include "led.h"
#include "st_port.h"
#include "st_port_inline.h"
#include "pcm_port.h"
#include "pcm_port_inline.h"
#include "st_port.h"
#include "st_chan.h"
#include "sys_port.h"
#include "sys_chan.h"
#include "card.h"

static void hfc_card_release(struct kref *kref)
{
	struct hfc_card *card = container_of(kref, struct hfc_card, kref);

	kfree(card);
}

struct hfc_card *hfc_card_get(struct hfc_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void hfc_card_put(struct hfc_card *card)
{
	kref_put(&card->kref, hfc_card_release);
}

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
	card->quartz_49 = !!value;
	hfc_card_update_r_ctrl(card);
	hfc_card_softreset(card);
	hfc_card_initialize_hw(card);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(double_clock, S_IRUGO | S_IWUSR,
		hfc_show_double_clock,
		hfc_store_double_clock);

//----------------------------------------------------------------------------

static ssize_t hfc_show_quartz_49(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->quartz_49 ? 1 : 0);
}

static ssize_t hfc_store_quartz_49(
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
	card->quartz_49 = !!value;
	hfc_card_update_r_brg_pcm_cfg(card);
	hfc_card_softreset(card);
	hfc_card_initialize_hw(card);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(quartz_49, S_IRUGO | S_IWUSR,
		hfc_show_quartz_49,
		hfc_store_quartz_49);

//----------------------------------------------------------------------------

static ssize_t hfc_show_pwm0(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n", card->pwm0);
}

static ssize_t hfc_store_pwm0(
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
	hfc_outb(card, hfc_R_PWM1, value);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(pwm0, S_IRUGO | S_IWUSR,
		hfc_show_pwm0,
		hfc_store_pwm0);

//----------------------------------------------------------------------------

static ssize_t hfc_show_pwm1(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%02x\n", card->pwm1);
}

static ssize_t hfc_store_pwm1(
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
	hfc_outb(card, hfc_R_PWM1, value);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR,
		hfc_show_pwm1,
		hfc_store_pwm1);

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
	hfc_card_update_bert_wd_md(card, 0);
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
	hfc_card_update_bert_wd_md(card, hfc_R_BERT_WD_MD_V_BERT_ERR);
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

		hfc_card_update_r_ram_misc(card);
		hfc_card_update_r_ctrl(card);

		hfc_card_softreset(card);
		hfc_card_initialize_hw(card);

		hfc_card_unlock(card);

		hfc_debug_card(card, 1, "RAM size set to %d\n", value);
	}

	return count;
}

static DEVICE_ATTR(ram_size, S_IRUGO | S_IWUSR,
		hfc_show_ram_size,
		hfc_store_ram_size);

//----------------------------------------------------------------------------
static ssize_t hfc_show_clock_source_config(
	struct device *device,
	DEVICE_ATTR_COMPAT
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
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	hfc_card_lock(card);

	if (count >= 4 && !strncmp(buf, "auto", 4)) {
		card->clock_source = -1;

		hfc_debug_card(card, 1, "Clock source set to auto\n");
	} else if(count > 0) {
		int clock_source;
		sscanf(buf, "%d", &clock_source);

		card->clock_source = clock_source;

		hfc_debug_card(card, 1, "Clock source set to %d\n", clock_source);
	}

	hfc_card_update_st_sync(card);
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(clock_source_config, S_IRUGO | S_IWUSR,
		hfc_show_clock_source_config,
		hfc_store_clock_source_config);

//----------------------------------------------------------------------------
static ssize_t hfc_show_clock_source_current(
	struct device *device,
	DEVICE_ATTR_COMPAT
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
		NULL);

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
	&dev_attr_quartz_49,
	&dev_attr_clock_source_config,
	&dev_attr_clock_source_current,
	&dev_attr_dip_switches,
	&dev_attr_ram_bandwidth,
	&dev_attr_ram_size,
	&dev_attr_bert_mode,
	&dev_attr_bert_err,
	&dev_attr_bert_sync,
	&dev_attr_bert_inv,
	&dev_attr_bert_cnt,
	&dev_attr_pwm0,
	&dev_attr_pwm1,
	NULL
};

int hfc_card_sysfs_create_files(
	struct hfc_card *card)
{
	struct device_attribute **attr = hfc_card_attributes;
	int err;

	while(*attr) {
		err = device_create_file(
			&card->pci_dev->dev,
			*attr);

		if (err < 0)
			goto err_create_file;

		attr++;
	}

	return 0;

err_create_file:

	while(*attr != *hfc_card_attributes) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr--;
	}

	return err;
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

void hfc_card_softreset(struct hfc_card *card)
{
	hfc_msg_card(card, KERN_INFO, "resetting\n");

	mb();
	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_SRES);
	mb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();

	hfc_wait_busy(card);
}

void hfc_card_update_r_ctrl(struct hfc_card *card)
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

void hfc_card_update_r_brg_pcm_cfg(struct hfc_card *card)
{
	if (card->quartz_49)
		hfc_outb(card, hfc_R_BRG_PCM_CFG,
			hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV_3_0 |
			hfc_R_BRG_PCM_CFG_V_ADDR_WRDLY_3NS);
	else
		hfc_outb(card, hfc_R_BRG_PCM_CFG,
			hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV_1_5 |
			hfc_R_BRG_PCM_CFG_V_ADDR_WRDLY_3NS);
}

void hfc_card_update_r_ram_misc(struct hfc_card *card)
{
	u8 ram_misc = 0;//hfc_R_RAM_MISC_V_FZ_MD;

	if (card->ram_size == 32)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_32K;
	else if (card->ram_size == 128)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_128K;
	else if (card->ram_size == 512)
		ram_misc |= hfc_R_RAM_MISC_V_RAM_SZ_512K;

	hfc_outb(card, hfc_R_RAM_MISC, ram_misc);
}

static void hfc_card_initialize_hw_nonsoft(struct hfc_card *card)
{
	// FIFO RAM configuration
	hfc_card_update_r_ram_misc(card);

	hfc_outb(card, hfc_R_FIFO_MD,
			hfc_R_FIFO_MD_V_FIFO_MD_00 |
			hfc_R_FIFO_MD_V_DF_MD_FSM |
			hfc_R_FIFO_MD_V_FIFO_SZ_00);

	hfc_sys_port_configure(&card->sys_port, 0, 0, 0);

	// IRQ MISC config
	card->regs.irqmsk_misc = 0;
	hfc_outb(card, hfc_R_IRQMSK_MISC, card->regs.irqmsk_misc);

	hfc_card_update_r_ctrl(card);
	hfc_card_update_r_brg_pcm_cfg(card);

	// Here is the place to configure:
	// R_CIRM
}

void hfc_card_update_pcm_md0(struct hfc_card *card, u8 otherbits)
{
	hfc_outb(card, hfc_R_PCM_MD0,
//		hfc_R_PCM_MD0_V_F0_LEN |
		otherbits |
		(card->pcm_port.master ?
			hfc_R_PCM_MD0_V_PCM_MD_MASTER :
			hfc_R_PCM_MD0_V_PCM_MD_SLAVE));
}

void hfc_card_update_pcm_md1(struct hfc_card *card)
{
	u8 pcm_md1 = 0;
	if (card->pcm_port.bitrate == 0) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_2MBIT;
	} else if (card->pcm_port.bitrate == 1) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_4MBIT;
	} else if (card->pcm_port.bitrate == 2) {
		pcm_md1 |= hfc_R_PCM_MD1_V_PCM_DR_8MBIT;
	}

	hfc_pcm_port_multireg_select(card, hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD1);
	hfc_outb(card, hfc_R_PCM_MD1, pcm_md1);
}

void hfc_card_update_st_sync(struct hfc_card *card)
{
	if (card->clock_source == -1)
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_AUTO_SYNC_ENABLED);
	else
		hfc_outb(card, hfc_R_ST_SYNC,
			hfc_R_ST_SYNC_V_SYNC_SEL(card->clock_source & 0x07) |
			hfc_R_ST_SYNC_V_AUTO_SYNC_DISABLED);
}

void hfc_card_update_bert_wd_md(struct hfc_card *card, u8 otherbits)
{
	hfc_outb(card, hfc_R_BERT_WD_MD,
		hfc_R_BERT_WD_MD_V_PAT_SEQ(card->bert_mode & 0x7) |
		otherbits);
}

void hfc_card_initialize_hw(struct hfc_card *card)
{
	int i;

	card->pwm0 = card->config->pwm0;
	card->pwm1 = card->config->pwm1;
	card->clock_source = -1;
	card->bert_mode = 0;

	card->pcm_port.master = TRUE;
	card->pcm_port.bitrate = 0;
	card->pcm_port.num_chans = 0;

	hfc_outb(card, hfc_R_PWM_MD,
		hfc_R_PWM_MD_V_PWM0_MD_PUSH |
		hfc_R_PWM_MD_V_PWM1_MD_PUSH);

	hfc_outb(card, hfc_R_PWM0, card->pwm0);
	hfc_outb(card, hfc_R_PWM1, card->pwm1);

	// Timer setup
	hfc_outb(card, hfc_R_TI_WD,
		hfc_R_TI_WD_V_EV_TS_8_192_S);
//		hfc_R_TI_WD_V_EV_TS_1_MS);

	hfc_card_update_pcm_md0(card, 0);
	hfc_card_update_pcm_md1(card);
	hfc_card_update_st_sync(card);
	hfc_card_update_bert_wd_md(card, 0);

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

	/* Timer interrupt enabled */
	hfc_outb(card, hfc_R_IRQMSK_MISC,
		hfc_R_IRQMSK_MISC_V_TI_IRQMSK);

	/*
	hfc_outb(card, hfc_R_RAM_ADDR2, 0x0);
	hfc_outb(card, hfc_R_RAM_ADDR1, 0x18);
	for(i=0; i<32; i++) {
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x00 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x40 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x80 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0xc0 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
	}
	*/

	for (i=0; i<card->num_st_ports; i++) {
		if (card->st_ports[i]) {
			hfc_st_port_select(card->st_ports[i]);
			hfc_st_port_update_st_ctrl0(card->st_ports[i]);
			hfc_st_port_update_st_ctrl2(card->st_ports[i]);
			hfc_st_port_update_st_clk_dly(card->st_ports[i]);
		}
	}

	if (card->num_st_ports == 4) {
		hfc_outb(card, hfc_R_GPIO_SEL,
			hfc_R_GPIO_SEL_V_GPIO_SEL4 |
			hfc_R_GPIO_SEL_V_GPIO_SEL5);
	}

	/* Enable interrupts */
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
	if (test_and_clear_bit(HFC_SYS_CHAN_TX_STATUS_STOPPED,
					&chan->tx.status))
		tasklet_schedule(&chan->tx.wake_tasklet);
}

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_sys_chan *chan)
{
	tasklet_schedule(&chan->rx.tasklet);
}

static inline void hfc_handle_timer_interrupt(struct hfc_card *card)
{
}

static inline void hfc_handle_state_interrupt(struct hfc_st_port *port)
{
	schedule_delayed_work(&port->state_change_work, 0);
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t hfc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t hfc_interrupt(int irq, void *dev_id)
#endif
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
			if (card->st_ports[i] &&
			    (irq_sci & (1 << card->st_ports[i]->id))) {
				hfc_handle_state_interrupt(card->st_ports[i]);
			}
		}
	}

	return IRQ_HANDLED;
}

struct hfc_card *hfc_card_create(
	struct hfc_card *card,
	struct pci_dev *pci_dev,
	struct hfc_card_config *card_config)
{
	int i;

	BUG_ON(card); /* Static allocation not supported */

	if (!card) {
		card = kmalloc(sizeof(*card), GFP_KERNEL);
		if (!card)
			return NULL;
	}

	memset(card, 0, sizeof(*card));

	kref_init(&card->kref);

	spin_lock_init(&card->lock);

	card->pci_dev = pci_dev;

	card->config = card_config;

	card->double_clock = card->config->double_clock;
	card->quartz_49 = card->config->quartz_49;
	card->ram_size = card->config->ram_size;

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		hfc_led_init(&card->leds[i], i, card);

	hfc_switch_create(&card->hfcswitch, card);

	hfc_sys_port_create(&card->sys_port, card, "sys");
	hfc_pcm_port_create(&card->pcm_port, card, "pcm");

	return card;
}

int hfc_card_register(struct hfc_card *card)
{
	int i;
	int err;

	hfc_card_get(card); /* Container is implicitly used */
	err = hfc_switch_register(&card->hfcswitch);
	if (err < 0)
		goto err_switch_register;

	hfc_card_get(card); /* Container is implicitly used */
	err = hfc_sys_port_register(&card->sys_port);
	if (err < 0)
		goto err_sys_port_register;

	hfc_card_get(card); /* Container is implicitly used */
	err = hfc_pcm_port_register(&card->pcm_port);
	if (err < 0)
		goto err_pcm_port_register;

	for (i=0; i<card->num_st_ports; i++) {
		if (card->st_ports[i]) {
			err = hfc_st_port_register(card->st_ports[i]);
			if (err < 0)
				goto err_st_port_register;
		}
	}

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
	for (i=card->num_st_ports - 1; i>=0; i--)
		if (card->st_ports[i])
			hfc_st_port_unregister(card->st_ports[i]);
err_st_port_register:
	hfc_pcm_port_unregister(&card->pcm_port);
err_pcm_port_register:
	hfc_sys_port_unregister(&card->sys_port);
err_sys_port_register:
	hfc_switch_unregister(&card->hfcswitch);
err_switch_register:

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
#define dev_name(&(card->pci_dev->dev)) card->pci_dev->dev.bus_id
#endif 
	hfc_msg(KERN_ERR, "HFC card at %s-%s registration failure: %d\n",
		card->pci_dev->dev.bus->name,
		dev_name(&(card->pci_dev->dev)),
		err);
	return err;
}

void hfc_card_unregister(struct hfc_card *card)
{
	int i;

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		hfc_led_remove(&card->leds[i]);

	hfc_card_sysfs_delete_files(card);

printk(KERN_DEBUG "hfc_card_unregister()\n");
	for (i=card->num_st_ports - 1; i>=0; i--)
		if (card->st_ports[i])
			hfc_st_port_unregister(card->st_ports[i]);

	hfc_pcm_port_unregister(&card->pcm_port);
	hfc_sys_port_unregister(&card->sys_port);

	hfc_switch_unregister(&card->hfcswitch);
}

int hfc_card_probe(struct hfc_card *card)
{
	int chip_type;
	int revision;
	int i;
	int err;

	err = pci_enable_device(card->pci_dev);
	if (err < 0) {
		goto err_pci_enable_device;
	}

	pci_write_config_word(card->pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(card->pci_dev, hfc_DRIVER_NAME);
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT,
			"cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(card->pci_dev);

	if (!card->pci_dev->irq) {
		hfc_msg_card(card, KERN_CRIT,
			"PCI device does not have an assigned IRQ!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(card->pci_dev, 1);
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
	err = request_irq(card->pci_dev->irq, &hfc_interrupt, SA_SHIRQ,
						hfc_DRIVER_NAME, card);
#else
	err = request_irq(card->pci_dev->irq, &hfc_interrupt, IRQF_SHARED,
						hfc_DRIVER_NAME, card);
#endif
	if (err < 0) {
		hfc_msg_card(card, KERN_CRIT, "unable to register irq\n");
		goto err_request_irq;
	}

	chip_type = hfc_R_CHIP_ID_V_CHIP_ID(hfc_inb(card, hfc_R_CHIP_ID));

	if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_4S) {
		card->num_st_ports = 4;
	} else if (chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_8S) {
		card->num_st_ports = 8;
	} else {
		hfc_msg_card(card, KERN_ERR,
			"unknown chip type '0x%02x'\n", chip_type);

		goto err_unknown_chip;
	}

	for (i=0; i<card->num_st_ports; i++) {
		char portid[10];

		if (card->config->ports_map & (1 << i)) {

			snprintf(portid, sizeof(portid), "st%d", i);

			card->st_ports[i] = hfc_st_port_create(NULL, card,
								portid, i);
			if (!card->st_ports[i]) {
				err = -ENOMEM;
				goto err_st_port_create;
			}
		
			if (card->num_st_ports == 4)
				card->st_ports[i]->led = &card->leds[i];
		}
	}

	revision = hfc_R_CHIP_RV_V_CHIP_RV(hfc_inb(card, hfc_R_CHIP_RV));

	if (revision < 1)
		goto err_unsupported_revision;

	hfc_msg_card(card, KERN_ERR,
		"HFC-%cS chip rev. %02x detected, "
		"using quartz=%d MHz, doubleclock=%d\n",
			chip_type == hfc_R_CHIP_ID_V_CHIP_ID_HFC_4S ?
				'4' : '8',
			revision,
			card->quartz_49 ? 49 : 24,
			card->double_clock ? 1 : 0);

	// Initialize all the card's components

	hfc_card_lock(card);
	hfc_card_initialize_hw_nonsoft(card);
	hfc_card_softreset(card);
	hfc_card_initialize_hw(card);
	hfc_card_unlock(card);

	return 0;

err_unsupported_revision:
	for(i=0; i<card->num_st_ports; i++) {
		if (card->st_ports[i]) {
			hfc_st_port_put(card->st_ports[i]);
			card->st_ports[i] = NULL;
		}
	}
err_st_port_create:
err_unknown_chip:
	free_irq(card->pci_dev->irq, card);
err_request_irq:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(card->pci_dev);
err_pci_request_regions:
err_pci_enable_device:
	pci_set_drvdata(card->pci_dev, NULL);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
#define dev_name(&(card->pci_dev->dev)) card->pci_dev->dev.bus_id
#endif
	hfc_msg(KERN_ERR, "HFC card at %s-%s initialization failure: %d\n",
		card->pci_dev->dev.bus->name,
		dev_name(&(card->pci_dev->dev)),
		err);
	return err;
}

void hfc_card_remove(struct hfc_card *card)
{
	hfc_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n",
		card->io_mem);

	/* softreset clears all pending interrupts */
	hfc_card_lock(card);
	hfc_card_softreset(card);
	hfc_card_unlock(card);

	/* There should be no interrupt from here on */

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);
	free_irq(card->pci_dev->irq, card);
	iounmap(card->io_mem);
	pci_release_regions(card->pci_dev);
	pci_disable_device(card->pci_dev);
}

void hfc_card_destroy(struct hfc_card *card)
{
	int i;

	/* Now put objects in a zombie state, releasing held
	 * reference. Be sure to destroy in reverse order as the
	 * kernel creates hidden kobj->parent reference.
	 */

	hfc_pcm_port_destroy(&card->pcm_port);
	hfc_sys_port_destroy(&card->sys_port);

	for (i=0; i<card->num_st_ports; i++) {
		if (card->st_ports[i]) {
			hfc_st_port_destroy(card->st_ports[i]);
			card->st_ports[i] = NULL;
		}
	}

	hfc_card_put(card);
}


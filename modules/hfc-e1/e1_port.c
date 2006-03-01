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

#include "e1_port.h"
#include "e1_port_inline.h"
#include "e1_chan.h"
#include "card.h"
#include "fifo_inline.h"

#ifdef DEBUG_CODE
#define hfc_debug_e1_port(port, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"e1:"					\
			format,					\
			(port)->card->pci_dev->dev.bus->name,	\
			(port)->card->pci_dev->dev.bus_id,	\
			## arg)
#else
#define hfc_debug_e1_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_e1_port(port, level, format, arg...)		\
	printk(level hfc_DRIVER_PREFIX				\
		"%s-%s:"					\
		"st%d:"						\
		format,						\
		(port)->card->pci_dev->dev.bus->name,		\
		(port)->card->pci_dev->dev.bus_id,		\
		(port)->id,					\
		## arg)

void hfc_e1_port_update_rx0(
	struct hfc_e1_port *port)
{
	u8 rx0 = 0;

	switch(port->rx_line_code) {
	case hfc_LINE_CODE_NRZ:
		rx0 |= hfc_R_RX0_V_RX_CODE_NRZ;
	break;

	case hfc_LINE_CODE_HDB3:
		rx0 |= hfc_R_RX0_V_RX_CODE_HDB3;
	break;

	case hfc_LINE_CODE_AMI:
		rx0 |= hfc_R_RX0_V_RX_CODE_AMI;
	break;

	default:
		BUG();
	}

	if (port->rx_full_baud)
		rx0 |= hfc_R_RX0_V_RX_FBAUD_FULL;
	else
		rx0 |= hfc_R_RX0_V_RX_FBAUD_HALF;

	hfc_outb(port->card, hfc_R_RX0, rx0);
}

void hfc_e1_port_update_tx0(
	struct hfc_e1_port *port)
{
	u8 tx0 = 0;

	switch(port->tx_line_code) {
	case hfc_LINE_CODE_NRZ:
		tx0 |= hfc_R_TX0_V_TX_CODE_NRZ;
	break;

	case hfc_LINE_CODE_HDB3:
		tx0 |= hfc_R_TX0_V_TX_CODE_HDB3;
	break;

	case hfc_LINE_CODE_AMI:
		tx0 |= hfc_R_TX0_V_TX_CODE_AMI;
	break;

	default:
		BUG();
	}

	if (port->tx_full_baud)
		tx0 |= hfc_R_TX0_V_TX_FBAUD_FULL;
	else
		tx0 |= hfc_R_TX0_V_TX_FBAUD_HALF;

//	if (port->visdn_port.enabled)
		tx0 |= hfc_R_TX0_V_OUT_EN;

	hfc_outb(port->card, hfc_R_TX0, tx0);
}

void hfc_e1_port_update_rx_sl0_cfg1(
	struct hfc_e1_port *port)
{
	u8 cfg1 = 0;

	if (port->rx_crc4)
		cfg1 |= hfc_R_RX_SL0_CFG1_V_RX_MF;

	hfc_outb(port->card, hfc_R_RX_SL0_CFG1, cfg1);
}

void hfc_e1_port_update_tx_sl0_cfg1(
	struct hfc_e1_port *port)
{
	u8 cfg1 = 0;

	if (port->tx_crc4)
		cfg1 |= hfc_R_TX_SL0_CFG1_V_TX_MF |
			hfc_R_TX_SL0_CFG1_V_TX_E |
			hfc_R_TX_SL0_CFG1_V_INV_E;

	hfc_outb(port->card, hfc_R_TX_SL0_CFG1, cfg1);
}

void hfc_e1_port_update_sync_ctrl(
	struct hfc_e1_port *port)
{
	u8 sc = 0;

	if (port->clock_source != hfc_CLOCK_SOURCE_LOOP)
		sc |= hfc_R_SYNC_CTRL_V_EXT_CLK_SYNC;

	if (port->clock_source == hfc_CLOCK_SOURCE_F0IO) {
		sc |= hfc_R_SYNC_CTRL_V_PCM_SYNC_F0IO;
		sc |= hfc_R_SYNC_CTRL_V_JATT_OFF;
	}
	
	hfc_outb(port->card, hfc_R_SYNC_CTRL, sc);
}

//----------------------------------------------------------------------------

static ssize_t hfc_show_role(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		port->nt_mode? "NT" : "TE");
}

static ssize_t hfc_store_role(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	if (count < 2)
		return count;

	hfc_card_lock(card);

	if (!strncmp(buf, "NT", 2) && !port->nt_mode) {
		port->nt_mode = TRUE;
	} else if (!strncmp(buf, "TE", 2) && port->nt_mode) {
		port->nt_mode = FALSE;
	}

//	hfc_e1_port_update_e1_ctrl_0(port);
//	hfc_e1_port_update_e1_clk_dly(port);

	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(role, S_IRUGO | S_IWUSR,
		hfc_show_role,
		hfc_store_role);

//----------------------------------------------------------------------------

static ssize_t hfc_show_clock_source(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	switch(port->clock_source) {
	case hfc_CLOCK_SOURCE_LOOP:
		return snprintf(buf, PAGE_SIZE, "loop\n");
	case hfc_CLOCK_SOURCE_F0IO:
		return snprintf(buf, PAGE_SIZE, "f0io\n");
	case hfc_CLOCK_SOURCE_SYNC_IN:
		return snprintf(buf, PAGE_SIZE, "sync_in\n");
	default:
		return 0;
	}
}

static ssize_t hfc_store_clock_source(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	if (count < 2)
		return count;

	hfc_card_lock(card);
	if (!strncmp(buf, "loop", strlen("loop")))
		port->clock_source = hfc_CLOCK_SOURCE_LOOP;
	else if (!strncmp(buf, "f0io", strlen("f0io")))
		port->clock_source = hfc_CLOCK_SOURCE_F0IO;
	else if (!strncmp(buf, "sync_in", strlen("sync_in")))
		port->clock_source = hfc_CLOCK_SOURCE_SYNC_IN;
	else {
		hfc_card_unlock(card);
		return -EINVAL;
	}

	hfc_e1_port_update_sync_ctrl(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(clock_source, S_IRUGO | S_IWUSR,
		hfc_show_clock_source,
		hfc_store_clock_source);

//----------------------------------------------------------------------------

static ssize_t hfc_show_jatt_sta(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(hfc_inb(port->card, hfc_R_JATT_STA) >> 5) & 0x03);
}

static VISDN_PORT_ATTR(jatt_sta, S_IRUGO,
		hfc_show_jatt_sta,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_l1_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%c%d\n",
		port->nt_mode?'G':'F',
		port->l1_state);
}

static VISDN_PORT_ATTR(l1_state, S_IRUGO,
		hfc_show_l1_state,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_frame_sync(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n",
			hfc_inb(port->card, hfc_R_SYNC_STA));
/*	return snprintf(buf, PAGE_SIZE, "%d\n",
			(hfc_inb(port->card, hfc_R_SYNC_STA) &
				hfc_R_SYNC_STA_V_FR_SYNC) ? 1 : 0);*/
}

static ssize_t hfc_store_rx_frame_sync(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_outb(card, hfc_R_RX_SL0_CFG0, hfc_R_RX_SL0_CFG0_V_RESYNC);
//	hfc_outb(card, hfc_R_RX_SL0_CFG1, hfc_R_RX_SL0_CFG1_V_RES_NMF);

	return count;
}

static VISDN_PORT_ATTR(rx_frame_sync, S_IRUGO | S_IWUSR,
		hfc_show_rx_frame_sync,
		hfc_store_rx_frame_sync);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_los(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(hfc_inb(port->card, hfc_R_SYNC_STA) &
				hfc_R_SYNC_STA_V_SIG_LOSS) ? 1 : 0);
}

static VISDN_PORT_ATTR(rx_los, S_IRUGO,
		hfc_show_rx_los,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_ais(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(hfc_inb(port->card, hfc_R_SYNC_STA) &
				hfc_R_SYNC_STA_V_AIS) ? 1 : 0);
}

static VISDN_PORT_ATTR(rx_ais, S_IRUGO,
		hfc_show_rx_ais,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_rai(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(hfc_inb(port->card, hfc_R_RX_SL0_0) &
				hfc_R_RX_SL0_0_V_A) ? 1 : 0);
}

static VISDN_PORT_ATTR(rx_rai, S_IRUGO,
		hfc_show_rx_rai,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_line_code(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	switch(port->rx_line_code) {
	case hfc_LINE_CODE_NRZ:
		return snprintf(buf, PAGE_SIZE, "NRZ\n");
	case hfc_LINE_CODE_HDB3:
		return snprintf(buf, PAGE_SIZE, "HDB3\n");
	case hfc_LINE_CODE_AMI:
		return snprintf(buf, PAGE_SIZE, "AMI\n");
	default:
		return 0;
	}
}

static ssize_t hfc_store_rx_line_code(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	if (!strncmp(buf, "NRZ", strlen("NRZ")))
		port->rx_line_code = hfc_LINE_CODE_NRZ;
	else if (!strncmp(buf, "HDB3", strlen("HDB3")))
		port->rx_line_code = hfc_LINE_CODE_HDB3;
	else if (!strncmp(buf, "AMI", strlen("AMI")))
		port->rx_line_code = hfc_LINE_CODE_AMI;
	else {
		hfc_card_unlock(card);
		return -EINVAL;
	}

	hfc_e1_port_update_rx0(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(rx_line_code, S_IRUGO | S_IWUSR,
		hfc_show_rx_line_code,
		hfc_store_rx_line_code);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_full_baud(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->rx_full_baud ? 1 : 0);
}

static ssize_t hfc_store_rx_full_baud(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;
	unsigned int value;

	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->rx_full_baud = !!value;
	hfc_e1_port_update_rx0(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(rx_full_baud, S_IRUGO | S_IWUSR,
		hfc_show_rx_full_baud,
		hfc_store_rx_full_baud);

//----------------------------------------------------------------------------

static ssize_t hfc_show_rx_crc4(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->rx_crc4 ? 1 : 0);
}

static ssize_t hfc_store_rx_crc4(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;
	unsigned int value;

	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->rx_crc4 = !!value;
	hfc_e1_port_update_rx_sl0_cfg1(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(rx_crc4, S_IRUGO | S_IWUSR,
		hfc_show_rx_crc4,
		hfc_store_rx_crc4);

//----------------------------------------------------------------------------

static ssize_t hfc_show_tx_line_code(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	switch(port->tx_line_code) {
	case hfc_LINE_CODE_NRZ:
		return snprintf(buf, PAGE_SIZE, "NRZ\n");
	case hfc_LINE_CODE_HDB3:
		return snprintf(buf, PAGE_SIZE, "HDB3\n");
	case hfc_LINE_CODE_AMI:
		return snprintf(buf, PAGE_SIZE, "AMI\n");
	default:
		return 0;
	}
}

static ssize_t hfc_store_tx_line_code(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	if (!strncmp(buf, "NRZ", sizeof("NRZ")))
		port->tx_line_code = hfc_LINE_CODE_NRZ;
	else if (!strncmp(buf, "HDB3", sizeof("HDB3")))
		port->tx_line_code = hfc_LINE_CODE_HDB3;
	else if (!strncmp(buf, "AMI", sizeof("AMI")))
		port->tx_line_code = hfc_LINE_CODE_AMI;
	else {
		hfc_card_unlock(card);
		return -EINVAL;
	}

	hfc_e1_port_update_tx0(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(tx_line_code, S_IRUGO | S_IWUSR,
		hfc_show_tx_line_code,
		hfc_store_tx_line_code);

//----------------------------------------------------------------------------

static ssize_t hfc_show_tx_full_baud(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->tx_full_baud ? 1 : 0);
}

static ssize_t hfc_store_tx_full_baud(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;
	unsigned int value;

	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->tx_full_baud = !!value;
	hfc_e1_port_update_tx0(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(tx_full_baud, S_IRUGO | S_IWUSR,
		hfc_show_tx_full_baud,
		hfc_store_tx_full_baud);

//----------------------------------------------------------------------------

static ssize_t hfc_show_tx_crc4(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->tx_crc4 ? 1 : 0);
}

static ssize_t hfc_store_tx_crc4(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;
	unsigned int value;

	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->tx_crc4 = !!value;
	hfc_e1_port_update_tx_sl0_cfg1(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(tx_crc4, S_IRUGO | S_IWUSR,
		hfc_show_tx_crc4,
		hfc_store_tx_crc4);

//----------------------------------------------------------------------------

static ssize_t hfc_show_fas_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->fas_cnt);
}

static VISDN_PORT_ATTR(fas_cnt, S_IRUGO,
		hfc_show_fas_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_vio_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->vio_cnt);
}

static VISDN_PORT_ATTR(vio_cnt, S_IRUGO,
		hfc_show_vio_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_crc_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->crc_cnt);
}

static VISDN_PORT_ATTR(crc_cnt, S_IRUGO,
		hfc_show_crc_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_e_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->e_cnt);
}

static VISDN_PORT_ATTR(e_cnt, S_IRUGO,
		hfc_show_e_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sa6_val13_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->sa6_val13_cnt);
}

static VISDN_PORT_ATTR(sa6_val13_cnt, S_IRUGO,
		hfc_show_sa6_val13_cnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sa6_val23_cnt(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%llu\n", port->sa6_val23_cnt);
}

static VISDN_PORT_ATTR(sa6_val23_cnt, S_IRUGO,
		hfc_show_sa6_val23_cnt,
		NULL);

//----------------------------------------------------------------------------

static struct visdn_port_attribute *hfc_e1_port_attributes[] =
{
	&visdn_port_attr_role,
	&visdn_port_attr_clock_source,
	&visdn_port_attr_jatt_sta,
	&visdn_port_attr_l1_state,

	&visdn_port_attr_rx_line_code,
	&visdn_port_attr_rx_full_baud,
	&visdn_port_attr_rx_crc4,

	&visdn_port_attr_tx_line_code,
	&visdn_port_attr_tx_full_baud,
	&visdn_port_attr_tx_crc4,

	&visdn_port_attr_rx_frame_sync,
	&visdn_port_attr_rx_los,
	&visdn_port_attr_rx_ais,
	&visdn_port_attr_rx_rai,

	&visdn_port_attr_fas_cnt,
	&visdn_port_attr_vio_cnt,
	&visdn_port_attr_crc_cnt,
	&visdn_port_attr_e_cnt,
	&visdn_port_attr_sa6_val13_cnt,
	&visdn_port_attr_sa6_val23_cnt,

	NULL
};

static void hfc_e1_port_update_led(struct hfc_e1_port *port)
{
	struct hfc_card *card = port->card;

	u8 sync_sta = hfc_inb(port->card, hfc_R_SYNC_STA);

	if (sync_sta & hfc_R_SYNC_STA_V_AIS)
		card->leds[0].color = HFC_LED_RED;
	else
		card->leds[0].color = HFC_LED_OFF;

	if (port->visdn_port.enabled)
		card->leds[1].color = HFC_LED_GREEN;
	else
		card->leds[1].color = HFC_LED_OFF;

	if (sync_sta & hfc_R_SYNC_STA_V_FR_SYNC)
		card->leds[2].color = HFC_LED_OFF;
	else
		card->leds[2].color = HFC_LED_RED;

	if (sync_sta & hfc_R_SYNC_STA_V_SIG_LOSS)
		card->leds[3].color = HFC_LED_OFF;
	else
		card->leds[3].color = HFC_LED_GREEN;

	hfc_led_update(&card->leds[0]);
	hfc_led_update(&card->leds[1]);
	hfc_led_update(&card->leds[2]);
	hfc_led_update(&card->leds[3]);
}

static void hfc_e1_port_fifo_update(struct hfc_e1_port *port)
{
	int i;

	for (i=0; i<ARRAY_SIZE(port->chans); i++) {
		struct hfc_sys_chan *sys_chan =
				port->chans[i].connected_sys_chan;

		if (sys_chan) {
			hfc_fifo_select(&sys_chan->rx_fifo);
			hfc_fifo_configure(&sys_chan->rx_fifo);

			hfc_fifo_select(&sys_chan->tx_fifo);
			hfc_fifo_configure(&sys_chan->tx_fifo);
		}
	}
}


static void hfc_e1_port_state_change_nt(
	struct hfc_e1_port *port,
	u8 old_state, u8 new_state)
{
//	struct hfc_card *card = port->card;

	hfc_debug_e1_port(port, 1,
		"layer 1 state = G%d => G%d\n",
		old_state,
		new_state);

	switch (new_state) {
	case 0:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*0*/
	break;

	case 1:
		schedule_delayed_work(&port->fifo_activation_work,
						50 * HZ / 1000);

		visdn_port_activated(&port->visdn_port);
	break;

	case 2:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*1*/
	break;

	case 3:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*2*/
	break;

	case 4:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*3*/
	break;

	case 5:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*4*/
	break;

	case 6:
		/* Do nothing */
	break;
	}
}

static void hfc_e1_port_state_change_te(
	struct hfc_e1_port *port,
	u8 old_state, u8 new_state)
{
	hfc_debug_e1_port(port, 1,
		"layer 1 state = F%d => F%d\n",
		old_state,
		new_state);

	switch (new_state) {
	case 0:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*0*/
	break;

	case 1:
		schedule_delayed_work(&port->fifo_activation_work,
						50 * HZ / 1000);

		visdn_port_activated(&port->visdn_port);
	break;

	case 2:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*1*/
	break;

	case 3:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*2*/
	break;

	case 4:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*3*/
	break;

	case 5:
		hfc_e1_port_fifo_update(port);

		if (old_state == 1)
			visdn_port_deactivated(&port->visdn_port);

		visdn_port_error_indication(&port->visdn_port); /*4*/
	break;

	case 6:
		/* Do nothing */
	break;
	}
}

/* TODO: use a tasklet here, or better, add a queue of state changes, otherwise
 * we may miss state changes.
 */
static void hfc_e1_port_state_change_work(void *data)
{
	struct hfc_e1_port *port = data;
	struct hfc_card *card = port->card;
	u8 rd_sta;
	u8 old_state;

	hfc_card_lock(card);

	old_state = port->l1_state;
	
	rd_sta = hfc_inb(card, hfc_R_E1_RD_STA);
	port->l1_state = hfc_R_E1_RD_STA_V_E1_STA(rd_sta);

	if (port->nt_mode) {
		hfc_e1_port_state_change_nt(port, old_state, port->l1_state);
	} else {
		hfc_e1_port_state_change_te(port, old_state, port->l1_state);
	}

	hfc_e1_port_update_led(port);

	hfc_card_unlock(card);
}

static void hfc_e1_port_fifo_activation_work(void *data)
{
	struct hfc_e1_port *port = data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	hfc_e1_port_fifo_update(port);

	hfc_card_unlock(card);
}

static void hfc_e1_port_counters_update_work(void *data)
{
	struct hfc_e1_port *port = data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	port->fas_cnt += hfc_inb(card, hfc_R_FAS_ECL) |
			hfc_inb(card, hfc_R_FAS_ECH) << 8;

	port->vio_cnt += hfc_inb(card, hfc_R_VIO_ECL) |
			hfc_inb(card, hfc_R_VIO_ECH) << 8;

	port->crc_cnt += hfc_inb(card, hfc_R_CRC_ECL) |
			hfc_inb(card, hfc_R_CRC_ECH) << 8;

	port->e_cnt += hfc_inb(card, hfc_R_E_ECL) |
			hfc_inb(card, hfc_R_E_ECH) << 8;

	port->sa6_val13_cnt += hfc_inb(card, hfc_R_SA6_VAL13_ECL) |
			hfc_inb(card, hfc_R_SA6_VAL13_ECH) << 8;

	port->sa6_val23_cnt += hfc_inb(card, hfc_R_SA6_VAL23_ECL) |
			hfc_inb(card, hfc_R_SA6_VAL23_ECH) << 8;

	hfc_e1_port_update_led(port);

	hfc_card_unlock(card);
}

static void hfc_e1_port_release(
	struct visdn_port *port)
{
	printk(KERN_DEBUG "hfc_e1_port_release()\n");

	// FIXME
}

static int hfc_e1_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	hfc_outb(port->card, hfc_R_E1_WR_STA,
		hfc_R_E1_WR_STA_V_E1_SET_STA(0));

	hfc_e1_port_update_tx0(port);

	hfc_e1_port_update_led(port);

	hfc_card_unlock(card);

	hfc_debug_e1_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_e1_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_outb(port->card, hfc_R_E1_WR_STA,
		hfc_R_E1_WR_STA_V_E1_SET_STA(0)|
		hfc_R_E1_WR_STA_V_E1_LD_STA);

	hfc_e1_port_update_tx0(port);

	hfc_e1_port_update_led(port);

	hfc_card_unlock(card);

	hfc_debug_e1_port(port, 2, "disabled\n");

	return 0;
}

static int hfc_e1_port_activate(
	struct visdn_port *visdn_port)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_card_unlock(card);

	return 0;
}

static int hfc_e1_port_deactivate(
	struct visdn_port *visdn_port)
{
	struct hfc_e1_port *port = to_e1_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_card_unlock(card);

	return 0;
}

struct visdn_port_ops hfc_e1_port_ops = {
	.owner		= THIS_MODULE,
	.release	= hfc_e1_port_release,

	.enable		= hfc_e1_port_enable,
	.disable	= hfc_e1_port_disable,

	.activate	= hfc_e1_port_activate,
	.deactivate	= hfc_e1_port_deactivate,
};

void hfc_e1_port_init(
	struct hfc_e1_port *port,
	struct hfc_card *card,
	const char *name)
{
	int i;

	port->card = card;

	INIT_WORK(&port->state_change_work,
		hfc_e1_port_state_change_work,
		port);

	INIT_WORK(&port->fifo_activation_work,
		hfc_e1_port_fifo_activation_work,
		port);

	INIT_WORK(&port->counters_update_work,
		hfc_e1_port_counters_update_work,
		port);

	port->nt_mode = FALSE;

	visdn_port_init(&port->visdn_port);
	port->visdn_port.ops = &hfc_e1_port_ops;
	port->visdn_port.driver_data = port;
	port->visdn_port.device = &card->pci_dev->dev;
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));

	for (i=0; i<ARRAY_SIZE(port->chans); i++)
		hfc_e1_chan_init(&port->chans[i], port, i, i);
}

int hfc_e1_port_register(
	struct hfc_e1_port *port)
{
	int i;
	int err;

	err = visdn_port_register(&port->visdn_port);
	if (err < 0)
		goto err_port_register;

	for (i=0; i<ARRAY_SIZE(port->chans); i++) {
		err = hfc_e1_chan_register(&port->chans[i]);
		if (err < 0)
			goto err_chan_register;
	}

	{
	struct visdn_port_attribute **attr = hfc_e1_port_attributes;

	while(*attr) {
		visdn_port_create_file(
			&port->visdn_port,
			*attr);

		attr++;
	}
	}

	return 0;

err_chan_register:
	visdn_port_unregister(&port->visdn_port);
err_port_register:

	return err;
}

void hfc_e1_port_unregister(
	struct hfc_e1_port *port)
{
	struct visdn_port_attribute **attr = hfc_e1_port_attributes;
	int i;

	while(*attr) {
		visdn_port_remove_file(
			&port->visdn_port,
			*attr);

		attr++;
	}

	for (i=0; i<ARRAY_SIZE(port->chans); i++)
		hfc_e1_chan_unregister(&port->chans[i]);

	visdn_port_unregister(&port->visdn_port);
}

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

#include "sys_port.h"
#include "card.h"
#include "card_inline.h"
#include "fifo_inline.h"

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

#ifdef DEBUG_CODE
#define hfc_debug_sys_port(port, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"sys:"					\
			format,					\
			(port)->card->pci_dev->dev.bus->name,	\
			(port)->card->pci_dev->dev.bus_id,	\
			## arg)
#else
#define hfc_debug_sys_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_sys_port(port, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s:"				\
		"sys:"					\
		format,					\
		(port)->card->pci_dev->dev.bus->name,	\
		(port)->card->pci_dev->dev.bus_id,	\
		## arg)


/*---------------------------------------------------------------------------*/

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
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_sys_port *port = to_sys_port(visdn_port);
	struct hfc_card *card = port->card;
	int i;

	*buf = '\0';

	sanprintf(buf, PAGE_SIZE,
		"\n       Receive                      Transmit\n"
		"FIFO#  F1 F2   Z1   Z2 Used Mode    F1 F2   Z1   Z2"
		" Used Mode Connected\n");

	hfc_card_lock(card);

	for (i=0; i<port->num_chans; i++) {
//		if (!card->fifos[i][RX].used && !card->fifos[i][TX].used)
//			continue;

		struct hfc_fifo *fifo_rx = &port->chans[i].rx_fifo;
		struct hfc_fifo *fifo_tx = &port->chans[i].tx_fifo;

		union hfc_fgroup f;
		union hfc_zgroup z;

		sanprintf(buf, PAGE_SIZE,
			"%2d   :", i);

		hfc_fifo_select(fifo_rx);

		f.f1f2 = hfc_inw(card, hfc_A_F12);
		z.z1z2 = hfc_inl(card, hfc_A_Z12);

		sanprintf(buf, PAGE_SIZE,
			" %02x %02x %04x %04x %4d %c%c%c  ",
			f.f1, f.f2, z.z1, z.z2,
			hfc_fifo_used_rx(fifo_rx),
			fifo_rx->framer_enabled ? 'H' : ' ',
			fifo_rx->enabled ? 'E' : ' ',
			hfc_fifo_is_running(fifo_rx) ? 'R' : ' ');

		hfc_fifo_select(fifo_tx);

		f.f1f2 = hfc_inw(card, hfc_A_F12);
		z.z1z2 = hfc_inl(card, hfc_A_Z12);

		sanprintf(buf, PAGE_SIZE,
			"   %02x %02x %04x %04x %4d %c%c%c  ",
			f.f1, f.f2, z.z1, z.z2,
			hfc_fifo_used_tx(fifo_tx),
			fifo_tx->framer_enabled ? 'H' : ' ',
			fifo_tx->enabled ? 'E' : ' ',
			hfc_fifo_is_running(fifo_tx) ? 'R' : ' ');

		if (fifo_rx->chan->connected_st_chan) {
			sanprintf(buf, PAGE_SIZE,
				" st%d:%s",
				fifo_rx->chan->connected_st_chan->port->id,
				fifo_rx->chan->connected_st_chan->
							visdn_chan.name);
		}

		if (fifo_rx->chan->connected_pcm_chan) {
			sanprintf(buf, PAGE_SIZE,
				" pcm:%s",
				fifo_rx->chan->connected_pcm_chan->
							visdn_chan.name);
		}

		sanprintf(buf, PAGE_SIZE, "\n");
	}

	hfc_card_unlock(card);

	return strlen(buf);
}

static VISDN_PORT_ATTR(fifo_state, S_IRUGO,
	hfc_show_fifo_state,
	NULL);

/*---------------------------------------------------------------------------*/

static struct visdn_port_attribute *hfc_sys_port_attributes[] =
{
	&visdn_port_attr_fifo_state,
	NULL
};

static inline void hfc_fsm_select_entry(
	struct hfc_card *card,
	int index)
{
	mb();
	hfc_outb(card, hfc_R_FSM_IDX, index);
	hfc_wait_busy(card); // NOT DOCUMENTED BUT MANDATORY!!!!!!!
	mb();
}

void hfc_upload_fsm_entry(
	struct hfc_card *card,
	struct hfc_fifo *fifo,
	struct hfc_st_chan *chan,
	struct hfc_fifo *next_fifo,
	int index)
{
	u8 subch_bits;

	hfc_debug_card(card, 3,
		"Seq #%d fifo [%d,%s] <=> chan [%d,%s] (%d:%s) ",
		index,
		fifo->hw_index,
		fifo->direction == RX ? "RX" : "TX",
		chan->hw_index,
		fifo->direction == RX ? "RX" : "TX",
		chan->port->id,
		chan->visdn_chan.name);

	hfc_fsm_select_entry(card, index);

	hfc_outb(card, hfc_A_CHANNEL,
		(fifo->direction == RX ?
			hfc_A_CHANNEL_V_CH_FDIR_RX :
			hfc_A_CHANNEL_V_CH_FDIR_TX) |
		hfc_A_CHANNEL_V_CH_FNUM(
			chan->hw_index));

	switch (fifo->subchannel_bit_count) {
		case 1: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_1; break;
		case 2: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_2; break;
		case 3: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_3; break;
		case 4: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_4; break;
		case 5: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_5; break;
		case 6: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_6; break;
		case 7: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_7; break;
		case 8: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_8; break;
		default:
			WARN_ON(1);
			subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_8;
	}

	hfc_outb(card, hfc_A_SUBCH_CFG, subch_bits);

	if (next_fifo) {
		hfc_outb(card, hfc_A_FIFO_SEQ,
			(next_fifo->direction == RX ?
				hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_RX :
				hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_TX) |
			hfc_A_FIFO_SEQ_V_NEXT_FIFO_NUM(
				next_fifo->hw_index));

		hfc_debug_cont(3, "=> fifo [%d,%s]\n",
			next_fifo->hw_index,
			next_fifo->direction == RX ? "RX" : "TX");

	} else {
		hfc_outb(card, hfc_A_FIFO_SEQ,
			hfc_A_FIFO_SEQ_V_SEQ_END);

		hfc_debug_cont(3, "END!\n");
	}
}

void hfc_sys_port_upload_fsm(
	struct hfc_sys_port *port)
{
	struct hfc_card *card = port->card;
	struct hfc_fifo *fifos[64];
	int nfifos = 0;

	int i;
	for (i=0; i<port->num_chans; i++) {
		if (port->chans[i].connected_st_chan ||
		    port->chans[i].connected_pcm_chan) {
			fifos[nfifos++] = &port->chans[i].tx_fifo;
			fifos[nfifos++] = &port->chans[i].rx_fifo;
		}
	}

	// Nothing inserted, let's close the empty list :)
	if (!nfifos) {
		hfc_fsm_select_entry(card, 0);

		hfc_outb(card, hfc_A_FIFO_SEQ,
			hfc_A_FIFO_SEQ_V_SEQ_END);

		return;
	}

	hfc_outb(card, hfc_R_FIRST_FIFO,
		(fifos[0]->direction == RX ?
			hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_RX :
			hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_TX) |
		hfc_R_FIRST_FIFO_V_FIRST_FIFO_NUM(fifos[0]->hw_index));

	{
	int index = 0;
	for (i=0; i<nfifos; i++) {
		if (i < nfifos-1)
			hfc_upload_fsm_entry(card, fifos[i],
				fifos[i]->chan->connected_st_chan,
					fifos[i+1], index);
		else
			hfc_upload_fsm_entry(card, fifos[i],
				fifos[i]->chan->connected_st_chan,
					NULL, index);

		index++;
	}
	}
}



static void hfc_sys_port_configure_fifo(
	struct hfc_fifo *fifo,
	struct hfc_fifo_config *fcfg,
	struct hfc_fifo_zone_config *fzcfg)
{
	fifo->f_min = fcfg->f_min;
	fifo->f_max = fcfg->f_max;
	fifo->f_num = fcfg->f_max - fcfg->f_min + 1;

	fifo->z_min = fzcfg->z_min;
	fifo->z_max = fzcfg->z_max;
	fifo->size = fzcfg->z_max - fzcfg->z_min + 1;
}

void hfc_sys_port_configure(
	struct hfc_sys_port *port,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz)
{
	struct hfc_fifo_config *fcfg = NULL;
	int i;

	// Find the correct configuration:

	for (i=0; i<ARRAY_SIZE(hfc_fifo_config); i++) {
		if (hfc_fifo_config[i].v_ram_sz == v_ram_sz &&
		    hfc_fifo_config[i].v_fifo_md == v_fifo_md &&
		    hfc_fifo_config[i].v_fifo_sz == v_fifo_sz) {

			fcfg = &hfc_fifo_config[i];
			break;
		}
	}

	hfc_debug_sys_port(port, 2, "Using FIFO config #%d\n", i);

	BUG_ON(!fcfg);

	port->num_chans = fcfg->num_fifos;

	for (i=0; i<fcfg->num_fifos; i++) {
		struct hfc_fifo_zone_config *fzcfg;

		if (i < fcfg->zone2_start_id)
			fzcfg = &fcfg->zone1;
		else
			fzcfg = &fcfg->zone2;

		hfc_sys_port_configure_fifo(&port->chans[i].rx_fifo,
							fcfg, fzcfg);
		hfc_sys_port_configure_fifo(&port->chans[i].tx_fifo,
							fcfg, fzcfg);

		hfc_debug_sys_port(port, 3,
			"FIFO %d zmin=%04x zmax=%04x"
			" fmin=%02x fmax=%02x\n",
		i,
		fzcfg->z_min,
		fzcfg->z_max,
		fcfg->f_min,
		fcfg->f_max);
	}
}

void hfc_sys_port_reconfigure(
	struct hfc_sys_port *port,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz)
{
	int i;

	for (i=0; i<port->num_chans; i++) {
		hfc_sys_chan_unregister(&port->chans[i]);
	}

	hfc_sys_port_configure(port, v_ram_sz, v_fifo_md, v_fifo_sz);

	for (i=0; i<port->num_chans; i++) {
		hfc_sys_chan_register(&port->chans[i]);
	}
}

static void hfc_sys_port_release(
	struct visdn_port *port)
{
	printk(KERN_DEBUG "hfc_sys_port_release()\n");
}

struct visdn_port_ops hfc_sys_port_ops = {
	.owner		= THIS_MODULE,
	.release	= hfc_sys_port_release,
	.enable		= NULL,
	.disable	= NULL,
};

void hfc_sys_port_init(
	struct hfc_sys_port *port,
	struct hfc_card *card,
	const char *name)
{
	int i;

	port->card = card;

	visdn_port_init(&port->visdn_port);
	port->visdn_port.ops = &hfc_sys_port_ops;
	port->visdn_port.device = &card->pci_dev->dev;
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));

	for (i=0; i<ARRAY_SIZE(port->chans); i++) {
		char chan_name[16];

		snprintf(chan_name, sizeof(chan_name), "sys%d", i);

		hfc_sys_chan_init(&port->chans[i], port, chan_name, i);
	}

	port->num_chans = 0;
}

int hfc_sys_port_register(
	struct hfc_sys_port *port)
{
	int err;
	int i;

	err = visdn_port_register(&port->visdn_port);
	if (err < 0)
		goto err_port_register;

	for (i=0; i<port->num_chans; i++) {
		err = hfc_sys_chan_register(&port->chans[i]);
		if (err < 0)
			goto err_chan_register;
	}

	{
	struct visdn_port_attribute **attr = hfc_sys_port_attributes;

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

void hfc_sys_port_unregister(
	struct hfc_sys_port *port)
{
	int i;
	struct visdn_port_attribute **attr = hfc_sys_port_attributes;

	while(*attr) {
		visdn_port_remove_file(
			&port->visdn_port,
			*attr);

		attr++;
	}

	for (i=0; i<port->num_chans; i++) {
		hfc_sys_chan_unregister(&port->chans[i]);
	}

	visdn_port_unregister(&port->visdn_port);
}

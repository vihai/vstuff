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

#include <kernel_config.h>

#include "chan.h"
#include "chan_sysfs.h"
#include "card.h"
#include "fsm.h"
#include "fifo_inline.h"
#include "st_port_inline.h"

static int hfc_bert_enable(struct hfc_chan_duplex *chan)
{
	struct hfc_card *card = chan->port->card;

	int err;

	hfc_card_lock(card);

	if (chan->status != HFC_CHAN_STATUS_FREE) {
		err = -EBUSY;
		goto err_busy;
	}

	struct hfc_fifo *fifo_rx;
	fifo_rx = hfc_allocate_fifo(card, RX);
	if (!fifo_rx) {
		err = -ENOMEM;
		goto err_allocate_fifo_rx;
	}

	struct hfc_fifo *fifo_tx;
	fifo_tx = hfc_allocate_fifo(card, TX);
	if (!fifo_tx) {
		err = -ENOMEM;
		goto err_allocate_fifo_tx;
	}

	chan->rx.fifo = fifo_rx;
	chan->rx.fifo->connected_chan = &chan->rx;
	chan->rx.fifo->bitrate = chan->visdn_chan.pars.bitrate;
	chan->rx.fifo->framing = VISDN_CHAN_FRAMING_TRANS;
	chan->rx.fifo->bit_reversed = FALSE;
	chan->rx.fifo->connect_to = HFC_FIFO_CONNECT_TO_ST;

	chan->tx.fifo = fifo_tx;
	chan->tx.fifo->connected_chan = &chan->tx;
	chan->tx.fifo->bitrate = chan->visdn_chan.pars.bitrate;
	chan->tx.fifo->framing = VISDN_CHAN_FRAMING_TRANS;
	chan->tx.fifo->bit_reversed = FALSE;
	chan->tx.fifo->connect_to = HFC_FIFO_CONNECT_TO_ST;

	hfc_upload_fsm(card);

	// RX
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_configure(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_BERT_EN);

	// TX
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_configure(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_BERT_EN);

	chan->status = HFC_CHAN_STATUS_OPEN_BERT;

	// Enable the channel in the port
	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl_0(chan->port);
	hfc_st_port_update_st_ctrl_2(chan->port);

	hfc_card_unlock(card);

	hfc_msg_chan(chan, KERN_INFO, "BERT enabled\n");

	return 0;

	hfc_deallocate_fifo(fifo_tx);
err_allocate_fifo_tx:
	hfc_deallocate_fifo(fifo_rx);
err_allocate_fifo_rx:
err_busy:

	hfc_card_unlock(card);

	return err;
}

static int hfc_bert_disable(
	struct hfc_chan_duplex *chan)
{
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	if (chan->status == HFC_CHAN_STATUS_OPEN_BERT) {
		chan->status = HFC_CHAN_STATUS_FREE;

		hfc_st_port_select(chan->port);
		hfc_st_port_update_st_ctrl_0(chan->port);
		hfc_st_port_update_st_ctrl_2(chan->port);

		// RX
		hfc_fifo_select(chan->rx.fifo);
		hfc_fifo_reset(chan->rx.fifo);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_IFF|
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_IRQ_MSK, 0);

		// TX
		hfc_fifo_select(chan->tx.fifo);
		hfc_fifo_reset(chan->tx.fifo);
		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_IFF|
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

		hfc_outb(card, hfc_A_IRQ_MSK, 0);

		hfc_msg_chan(chan, KERN_INFO, "BERT disabled\n");
	}

	hfc_card_unlock(card);

	return 0;
}


static ssize_t hfc_show_bert_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->status == HFC_CHAN_STATUS_OPEN_BERT ? 1 : 0);
}

static ssize_t hfc_store_bert_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	int err;

	int enabled;
	sscanf(buf, "%d", &enabled);

// FIXME TODO one-way only

	if (enabled)
		err = hfc_bert_enable(chan);
	else
		err = hfc_bert_disable(chan);

	if (err < 0)
		return err;

	return count;
}

static VISDN_CHAN_ATTR(bert_enabled, S_IRUGO | S_IWUSR,
		hfc_show_bert_enabled,
		hfc_store_bert_enabled);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	hfc_st_port_select(port);

	// TODO sleep until complete frame read
	int bits = hfc_A_ST_SQ_RD_V_ST_SQ_RD(hfc_inb(card, hfc_A_ST_SQ_RD));

	hfc_card_unlock(card);

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	hfc_card_lock(card);
	hfc_st_port_select(port);
	hfc_outb(card, hfc_A_ST_SQ_WR, value);
	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_bits, S_IRUGO | S_IWUSR,
		hfc_show_sq_bits,
		hfc_store_sq_bits);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->sq_enabled ? 1 : 0));

}

static ssize_t hfc_store_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);

	port->sq_enabled = !!value;

	hfc_st_port_select(port);
	hfc_st_port_update_st_ctrl_0(port);

	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);


static int hfc_chan_sysfs_create_files_DB(
	struct hfc_chan_duplex *chan)
{
	int err;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);
	if (err < 0)
		goto err_create_file_bert_enabled;

	return 0;

	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_bert_enabled);
err_create_file_bert_enabled:

	return err;
}

int hfc_chan_sysfs_create_files_D(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_E(
	struct hfc_chan_duplex *chan)
{
	return 0;
}

int hfc_chan_sysfs_create_files_B(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_SQ(
	struct hfc_chan_duplex *chan)
{
	int err;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_bits);
	if (err < 0)
		goto err_create_file_sq_bits;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_enabled);
	if (err < 0)
		goto err_create_file_sq_enabled;

	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_enabled);
err_create_file_sq_enabled:
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_bits);
err_create_file_sq_bits:

	return err;
}

void hfc_chan_sysfs_delete_files_DB(
	struct hfc_chan_duplex *chan)
{
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_bert_enabled);
}

void hfc_chan_sysfs_delete_files_D(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_E(
	struct hfc_chan_duplex *chan)
{
}

void hfc_chan_sysfs_delete_files_B(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_SQ(
	struct hfc_chan_duplex *chan)
{
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_enabled);
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_bits);
}


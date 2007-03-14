/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>

#include "card.h"
#include "st_chan.h"
#include "st_port.h"
#include "st_port_inline.h"

#ifdef DEBUG_CODE
#define hfc_debug_st_chan(chan, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st%d:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pci_dev->dev.bus->name,	\
			(chan)->port->card->pci_dev->dev.bus_id,	\
			(chan)->port->id,				\
			(chan)->ks_node.kobj.name,			\
			## arg)
#else
#define hfc_debug_st_chan(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_st_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st%d:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pci_dev->dev.bus->name,		\
		(chan)->port->card->pci_dev->dev.bus_id,		\
		(chan)->port->id,					\
		(chan)->ks_node.kobj.name,				\
		## arg)

//----------------------------------------------------------------------------

#if 0
static int hfc_bert_enable(struct hfc_st_chan *chan)
{
	struct hfc_card *card = chan->port->card;

	int err;

	hfc_card_lock(card);

	if (chan->status != HFC_ST_CHAN_STATUS_FREE) {
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

	chan->status = HFC_ST_CHAN_STATUS_OPEN_BERT;

	// Enable the channel in the port
	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl0(chan->port);
	hfc_st_port_update_st_ctrl1(chan->port);
	hfc_st_port_update_st_ctrl2(chan->port);

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
	struct hfc_st_chan *chan)
{
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	if (chan->status == HFC_ST_CHAN_STATUS_OPEN_BERT) {
		chan->status = HFC_ST_CHAN_STATUS_FREE;

		hfc_st_port_select(chan->port);
		hfc_st_port_update_st_ctrl0(chan->port);
		hfc_st_port_update_st_ctrl2(chan->port);

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
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->status == HFC_ST_CHAN_STATUS_OPEN_BERT ? 1 : 0);
}

static ssize_t hfc_store_bert_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

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
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;
	int bits;

	hfc_card_lock(card);

	hfc_st_port_select(port);

	// TODO sleep until complete frame read
	bits = hfc_A_ST_SQ_RD_V_ST_SQ_RD(hfc_inb(card, hfc_A_ST_SQ_RD));

	hfc_card_unlock(card);

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
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
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->sq_enabled ? 1 : 0));

}

static ssize_t hfc_store_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);

	port->sq_enabled = !!value;

	hfc_st_port_select(port);
	hfc_st_port_update_st_ctrl0(port);

	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);
#endif
//----------------------------------------------------------------------------

static int hfc_st_chan_sysfs_create_files_DB(
	struct hfc_st_chan *chan)
{
	int err;

/*	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);
	if (err < 0)
		goto err_create_file_bert_enabled;*/

	return 0;

/*	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);*/

//err_create_file_bert_enabled:

	return err;
}

int hfc_st_chan_sysfs_create_files_D(
	struct hfc_st_chan *chan)
{
	return hfc_st_chan_sysfs_create_files_DB(chan);
}

int hfc_st_chan_sysfs_create_files_E(
	struct hfc_st_chan *chan)
{
	return 0;
}

int hfc_st_chan_sysfs_create_files_B(
	struct hfc_st_chan *chan)
{
	return hfc_st_chan_sysfs_create_files_DB(chan);
}

int hfc_st_chan_sysfs_create_files_SQ(
	struct hfc_st_chan *chan)
{
	int err;

/*	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_bits);
	if (err < 0)
		goto err_create_file_sq_bits;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_enabled);
	if (err < 0)
		goto err_create_file_sq_enabled;*/

	return 0;

/*	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_enabled);
err_create_file_sq_enabled:
	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_bits);
err_create_file_sq_bits:*/

	return err;
}

void hfc_st_chan_sysfs_delete_files_DB(
	struct hfc_st_chan *chan)
{
/*	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);*/
}

void hfc_st_chan_sysfs_delete_files_D(
	struct hfc_st_chan *chan)
{
	hfc_st_chan_sysfs_delete_files_DB(chan);
}

void hfc_st_chan_sysfs_delete_files_E(
	struct hfc_st_chan *chan)
{
}

void hfc_st_chan_sysfs_delete_files_B(
	struct hfc_st_chan *chan)
{
	hfc_st_chan_sysfs_delete_files_DB(chan);
}

void hfc_st_chan_sysfs_delete_files_SQ(
	struct hfc_st_chan *chan)
{
/*	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_enabled);

	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_bits);*/
}

//----------------------------------------------------------------------------

static void hfc_st_chan_node_release(struct ks_node *node)
{
	struct hfc_st_chan *chan = container_of(node, struct hfc_st_chan,
							ks_node);

printk(KERN_DEBUG "hfc_st_chan_node_release()\n");

	hfc_st_port_put(chan->port);
}

static struct ks_node_ops hfc_st_chan_node_ops = {
	.owner		= THIS_MODULE,
	.refcnt		= &module_refcnt,
	.release	= hfc_st_chan_node_release,
};

//----------------------------------------------------------------------------

static void hfc_st_chan_rx_chan_release(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);

printk(KERN_DEBUG "hfc_st_chan_rx_chan_release()\n");

	hfc_st_port_put(chan_rx->chan->port);
}

static int hfc_st_chan_rx_chan_connect(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;

	hfc_debug_st_chan(chan, 2, "RX connected\n");

	return 0;
}

static void hfc_st_chan_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;

	hfc_debug_st_chan(chan, 2, "RX disconnected\n");
}

static int hfc_st_chan_rx_chan_open(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;
	int err;

	hfc_card_lock(card);

	if (chan_rx->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_st_chan(chan, 1, "RX open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->id != D && chan->id != E &&
	    chan->id != B1 && chan->id != B2) {
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	chan_rx->status = HFC_ST_CHAN_STATUS_OPEN;

	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl2(chan->port);

	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "RX channel opened\n");

	return 0;

	chan_rx->status = HFC_ST_CHAN_STATUS_FREE;
err_invalid_chan:
err_channel_busy:
	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "RX channel opened failed: %d\n", err);

	return err;
}

static void hfc_st_chan_rx_chan_close(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);
	chan_rx->status = HFC_ST_CHAN_STATUS_FREE;
	/* Enable channel in the port */
	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl2(chan->port);
	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "RX channel closed.\n");
}

static int hfc_st_chan_rx_chan_start(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;

	hfc_debug_st_chan(chan, 1, "RX channel started.\n");

	return 0;
}

static void hfc_st_chan_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_rx *chan_rx = to_st_chan_rx(ks_chan);
	struct hfc_st_chan *chan = chan_rx->chan;

	hfc_debug_st_chan(chan, 1, "RX channel stopped\n");
}

struct ks_chan_ops hfc_st_chan_rx_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_st_chan_rx_chan_release,
	.connect		= hfc_st_chan_rx_chan_connect,
	.disconnect		= hfc_st_chan_rx_chan_disconnect,
	.open			= hfc_st_chan_rx_chan_open,
	.close			= hfc_st_chan_rx_chan_close,
	.start			= hfc_st_chan_rx_chan_start,
	.stop			= hfc_st_chan_rx_chan_stop,
};

/*----------------------------------------------------------------------------*/

static void hfc_st_chan_tx_chan_release(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);

printk(KERN_DEBUG "hfc_st_chan_tx_chan_release()\n");

	hfc_st_port_put(chan_tx->chan->port);
}

static int hfc_st_chan_tx_chan_connect(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;

	hfc_debug_st_chan(chan, 2, "TX connected\n");

	return 0;
}

static void hfc_st_chan_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;

	hfc_debug_st_chan(chan, 2, "TX disconnected\n");

/*	hfc_outb(card, hfc_R_RAM_ADDR2, 0x0);
	hfc_outb(card, hfc_R_RAM_ADDR1, 0x18);
	for(i = port->id * 4; i < (port->id+1) * 4; i++) {
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x00 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x40 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x80 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0xc0 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0xff);
	}*/
}

static int hfc_st_chan_tx_chan_open(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;
	int err;

	hfc_card_lock(card);

	if (chan_tx->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_st_chan(chan, 1, "TX open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->id != D && chan->id != E &&
	    chan->id != B1 && chan->id != B2) {
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	chan_tx->status = HFC_ST_CHAN_STATUS_OPEN;

	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl0(chan->port);
	hfc_st_port_update_st_ctrl1(chan->port);

	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "TX channel opened\n");

	return 0;

	chan_tx->status = HFC_ST_CHAN_STATUS_FREE;
err_invalid_chan:
err_channel_busy:
	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "TX channel opened failed: %d\n", err);

	return err;
}

static void hfc_st_chan_tx_chan_close(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);
	chan_tx->status = HFC_ST_CHAN_STATUS_FREE;

	hfc_st_port_select(chan->port);
	/* Disable channel in the port */
	hfc_st_port_update_st_ctrl0(chan->port);
	hfc_st_port_update_st_ctrl1(chan->port);
	hfc_card_unlock(card);

	hfc_debug_st_chan(chan, 1, "TX channel closed.\n");
}

static int hfc_st_chan_tx_chan_start(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;

	hfc_debug_st_chan(chan, 1, "TX channel started.\n");

	return 0;
}

static void hfc_st_chan_tx_chan_stop(struct ks_chan *ks_chan)
{
	struct hfc_st_chan_tx *chan_tx = to_st_chan_tx(ks_chan);
	struct hfc_st_chan *chan = chan_tx->chan;

	hfc_debug_st_chan(chan, 1, "TX channel stopped\n");
}

struct ks_chan_ops hfc_st_chan_tx_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_st_chan_tx_chan_release,
	.connect		= hfc_st_chan_tx_chan_connect,
	.disconnect		= hfc_st_chan_tx_chan_disconnect,
	.open			= hfc_st_chan_tx_chan_open,
	.close			= hfc_st_chan_tx_chan_close,
	.start			= hfc_st_chan_tx_chan_start,
	.stop			= hfc_st_chan_tx_chan_stop,
};

//----------------------------------------------------------------------------

static void hfc_st_chan_duplex_release(struct ks_duplex *ks_duplex)
{
	struct hfc_st_chan *chan = container_of(ks_duplex,
					struct hfc_st_chan, ks_duplex);

printk(KERN_DEBUG "hfc_st_chan_duplex_release()\n");

	hfc_st_port_put(chan->port);
}

static struct ks_duplex_ops hfc_st_chan_duplex_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_st_chan_duplex_release,
};

static struct hfc_st_chan_rx *hfc_st_chan_rx_create(
	struct hfc_st_chan_rx *chan_rx,
	struct hfc_st_chan *chan)
{
	BUG_ON(!chan_rx); /* Dynamic allocation not supported */
	BUG_ON(!chan);

	chan_rx->chan = chan;

	ks_chan_create(&chan_rx->ks_chan,
			&hfc_st_chan_rx_chan_ops, "rx",
			&chan->ks_duplex,
			&chan->ks_node.kobj,
			&chan->ks_node,
			&chan->port->card->hfcswitch.ks_node);

	return chan_rx;
}

static struct hfc_st_chan_tx *hfc_st_chan_tx_create(
	struct hfc_st_chan_tx *chan_tx,
	struct hfc_st_chan *chan)
{
	BUG_ON(!chan_tx); /* Dynamic allocation not supported */
	BUG_ON(!chan);

	chan_tx->chan = chan;

	ks_chan_create(&chan_tx->ks_chan,
			&hfc_st_chan_tx_chan_ops, "tx",
			&chan->ks_duplex,
			&chan->ks_node.kobj,
			&chan->port->card->hfcswitch.ks_node,
			&chan->ks_node);

	return chan_tx;
}

struct hfc_st_chan *hfc_st_chan_create(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	int subchannel_bit_count,
	int subchannel_bit_start,
	int native_bitrate)
{
	BUG_ON(!chan); /* Dynamic allocation not supported */
	BUG_ON(!port);
	BUG_ON(!name);

	chan->port = port;
	chan->id = id;
	chan->hw_index = hw_index;
	chan->subchannel_bit_count = subchannel_bit_count;
	chan->subchannel_bit_start = subchannel_bit_start;

	ks_node_create(&chan->ks_node,
			&hfc_st_chan_node_ops, name,
			&port->visdn_port.kobj);

	hfc_st_port_get(port);
	ks_duplex_create(&chan->ks_duplex,
			&hfc_st_chan_duplex_ops,
			"duplex",
			&chan->ks_node.kobj);

	hfc_st_port_get(chan->port);
	hfc_st_chan_rx_create(&chan->rx, chan);

	hfc_st_port_get(chan->port);
	hfc_st_chan_tx_create(&chan->tx, chan);

	return chan;
}

static int hfc_st_chan_rx_register(struct hfc_st_chan_rx *chan)
{
	int err;

	err = ks_chan_register(&chan->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&chan->ks_chan);
err_chan_register:

	return err;
}

static void hfc_st_chan_rx_unregister(struct hfc_st_chan_rx *chan)
{
	ks_chan_unregister(&chan->ks_chan);
}

static void hfc_st_chan_rx_destroy(struct hfc_st_chan_rx *chan)
{
	ks_chan_destroy(&chan->ks_chan);
}

static int hfc_st_chan_tx_register(struct hfc_st_chan_tx *chan)
{
	int err;

	err = ks_chan_register(&chan->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&chan->ks_chan);
err_chan_register:

	return err;
}

static void hfc_st_chan_tx_unregister(struct hfc_st_chan_tx *chan)
{
	ks_chan_unregister(&chan->ks_chan);
}

static void hfc_st_chan_tx_destroy(struct hfc_st_chan_tx *chan)
{
	ks_chan_destroy(&chan->ks_chan);
}

int hfc_st_chan_register(struct hfc_st_chan *chan)
{
	int err;

	err = ks_node_register(&chan->ks_node);
	if (err < 0)
		goto err_node_register;

	err = ks_duplex_register(&chan->ks_duplex);
	if (err < 0)
		goto err_duplex_register;

	err = hfc_st_chan_rx_register(&chan->rx);
	if (err < 0)
		goto err_st_chan_rx_register;

	err = hfc_st_chan_tx_register(&chan->tx);
	if (err < 0)
		goto err_st_chan_tx_register;

	switch(chan->id) {
	case D:
		hfc_st_chan_sysfs_create_files_D(chan);
	break;

	case B1:
	case B2:
		hfc_st_chan_sysfs_create_files_B(chan);
	break;

	case E:
		hfc_st_chan_sysfs_create_files_E(chan);
	break;

	case SQ:
		hfc_st_chan_sysfs_create_files_SQ(chan);
	break;
	}

	return 0;

	hfc_st_chan_tx_unregister(&chan->tx);
err_st_chan_tx_register:
	hfc_st_chan_rx_unregister(&chan->rx);
err_st_chan_rx_register:
	ks_duplex_unregister(&chan->ks_duplex);
err_duplex_register:
	ks_node_unregister(&chan->ks_node);
err_node_register:

	return err;
}

void hfc_st_chan_unregister(struct hfc_st_chan *chan)
{
	switch(chan->id) {
	case D:
		hfc_st_chan_sysfs_delete_files_D(chan);
	break;

	case B1:
	case B2:
		hfc_st_chan_sysfs_delete_files_B(chan);
	break;

	case E:
		hfc_st_chan_sysfs_delete_files_E(chan);
	break;

	case SQ:
		hfc_st_chan_sysfs_delete_files_SQ(chan);
	break;
	}

	hfc_st_chan_tx_unregister(&chan->tx);
	hfc_st_chan_rx_unregister(&chan->rx);

	ks_duplex_unregister(&chan->ks_duplex);
	ks_node_unregister(&chan->ks_node);
}

void hfc_st_chan_destroy(struct hfc_st_chan *chan)
{
	hfc_st_chan_tx_destroy(&chan->tx);
	hfc_st_chan_rx_destroy(&chan->rx);

	ks_duplex_destroy(&chan->ks_duplex);

	ks_node_destroy(&chan->ks_node);
}

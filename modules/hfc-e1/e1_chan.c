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







/*	hfc_outb(card, hfc_R_RAM_ADDR2, 0x0);
	hfc_outb(card, hfc_R_RAM_ADDR1, 0x18);
	for(i = port->id * 4; i < (port->id+1) * 4; i++) {
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x00 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0x00);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x40 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0x00);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0x80 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0x00);
		hfc_outb(card, hfc_R_RAM_ADDR0, 0xc0 + i * 2);
		hfc_outb(card, hfc_R_RAM_DATA, 0x00);
	}*/







#include <linux/kernel.h>
#include <linux/delay.h>

#include "card.h"
#include "e1_chan.h"
#include "e1_port.h"
#include "e1_port_inline.h"

#ifdef DEBUG_CODE
#define hfc_debug_e1_chan(chan, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"e1:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pci_dev->dev.bus->name,	\
			(chan)->port->card->pci_dev->dev.bus_id,	\
			(chan)->visdn_chan.name,			\
			## arg)
#else
#define hfc_debug_e1_chan(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_e1_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"e1:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pci_dev->dev.bus->name,		\
		(chan)->port->card->pci_dev->dev.bus_id,		\
		(chan)->visdn_chan.name,				\
		## arg)

//----------------------------------------------------------------------------

static int hfc_bert_enable(struct hfc_e1_chan *chan)
{
/*	struct hfc_card *card = chan->port->card;

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
*/
	return 0;
}

static int hfc_bert_disable(
	struct hfc_e1_chan *chan)
{
/*
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	if (chan->status == HFC_ST_CHAN_STATUS_OPEN_BERT) {
		chan->status = HFC_ST_CHAN_STATUS_FREE;

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
*/
	return 0;
}


static ssize_t hfc_show_bert_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->status == HFC_ST_CHAN_STATUS_OPEN_BERT ? 1 : 0);
}

static ssize_t hfc_store_bert_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);

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

static void hfc_e1_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_e1_chan_release()\n");

	// FIXME
}

static int hfc_e1_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;
	int err;

	hfc_card_lock(card);

	if (chan->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_e1_chan(chan, 1, "open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	chan->status = HFC_ST_CHAN_STATUS_OPEN;

	hfc_card_unlock(card);

	hfc_debug_e1_chan(chan, 1, "channel opened.\n");

	return 0;

	chan->status = HFC_ST_CHAN_STATUS_FREE;
err_channel_busy:
	hfc_card_unlock(card);

	hfc_debug_e1_chan(chan, 1, "channel opened failed: %d\n", err);

	return err;
}

static int hfc_e1_chan_close(struct visdn_chan *visdn_chan)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);
	chan->status = HFC_ST_CHAN_STATUS_FREE;
	hfc_card_unlock(card);

	hfc_debug_e1_chan(chan, 1, "channel closed\n");

	return 0;
}

static int hfc_e1_chan_start(struct visdn_chan *visdn_chan)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);

	hfc_debug_e1_chan(chan, 1, "channel start\n");

	return 0;
}

static int hfc_e1_chan_stop(struct visdn_chan *visdn_chan)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_chan);

	hfc_debug_e1_chan(chan, 1, "channel stop\n");

	return 0;
}

static int hfc_e1_chan_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_e1_chan *chan = to_e1_chan(visdn_leg->chan);
//	struct hfc_e1_port *port = chan->port;
	struct hfc_card *card = chan->port->card;
	int err;

	hfc_debug_e1_chan(chan, 2, "connecting to %s\n",
		visdn_leg2->chan->kobj.name);

	if (visdn_chan_lock_interruptible(visdn_leg->chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_e1_chan(chan, 1, "connect failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

/*	if (chan->id != D && chan->id != E &&
	    chan->id != B1 && chan->id != B2) {
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}*/

	if (visdn_leg2->chan->chan_class == &hfc_sys_chan_class) {
		chan->connected_sys_chan = to_sys_chan(visdn_leg2->chan);
	} else if (visdn_leg2->chan->chan_class == &hfc_pcm_chan_class) {
		chan->connected_pcm_chan = to_pcm_chan(visdn_leg2->chan);
	} else {
		WARN_ON(1);
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_leg->chan);

	hfc_debug_e1_chan(chan, 1, "channel connected.\n");

	return 0;

	chan->status = HFC_ST_CHAN_STATUS_FREE;
//err_invalid_chan:
err_channel_busy:
	visdn_chan_unlock(visdn_leg->chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static void hfc_e1_chan_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
}

static struct visdn_chan_ops hfc_e1_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_e1_chan_release,
	.open			= hfc_e1_chan_open,
	.close			= hfc_e1_chan_close,
	.start			= hfc_e1_chan_start,
	.stop			= hfc_e1_chan_stop,
};

static struct visdn_leg_ops hfc_e1_chan_leg_ops = {
	.owner			= THIS_MODULE,

	.connect		= hfc_e1_chan_connect,
	.disconnect		= hfc_e1_chan_disconnect,
};

struct visdn_chan_class hfc_e1_chan_class =
{
	.name	= "e1"
};

void hfc_e1_chan_init(
	struct hfc_e1_chan *chan,
	struct hfc_e1_port *port,
	int id,
	int hw_index)
{
	chan->port = port;
	chan->status = HFC_ST_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	visdn_chan_init(&chan->visdn_chan);

	chan->visdn_chan.ops = &hfc_e1_chan_ops;
	chan->visdn_chan.port = &port->visdn_port;
	chan->visdn_chan.chan_class = &hfc_e1_chan_class;

	chan->visdn_chan.leg_a.cxc = &port->card->cxc.visdn_cxc;
	chan->visdn_chan.leg_a.ops = &hfc_e1_chan_leg_ops;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.cxc = NULL;
	chan->visdn_chan.leg_b.ops = NULL;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.mtu = -1;

	snprintf(chan->visdn_chan.name, sizeof(chan->visdn_chan.name),
		"%d", id);
	chan->visdn_chan.driver_data = chan;
}

int hfc_e1_chan_register(struct hfc_e1_chan *chan)
{
	int err;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);
	if (err < 0)
		goto err_create_file_bert_enabled;

	return 0;

	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);
err_create_file_bert_enabled:
	visdn_chan_unregister(&chan->visdn_chan);
err_chan_register:

	return err;
}

void hfc_e1_chan_unregister(struct hfc_e1_chan *chan)
{
	visdn_chan_remove_file(
		&chan->visdn_chan,
		&visdn_chan_attr_bert_enabled);

	visdn_chan_unregister(&chan->visdn_chan);
}

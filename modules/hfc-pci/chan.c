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

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>
#include <linux/visdn/softcxc.h>

#include "chan.h"
#include "card.h"
#include "st_port.h"
#include "fifo.h"
#include "fifo_inline.h"

#define to_chan_duplex(chan) \
	container_of(chan, struct hfc_chan_duplex, visdn_chan)

#ifdef DEBUG_CODE
#define hfc_debug_chan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pcidev->dev.bus->name,	\
			(chan)->port->card->pcidev->dev.bus_id,		\
			(chan)->visdn_chan.name,			\
			## arg)

#define hfc_debug_schan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st:"						\
			"chan[%s,%s] "					\
			format,						\
			(chan)->chan->port->card->pcidev->dev.bus->name,\
			(chan)->chan->port->card->pcidev->dev.bus_id,	\
			(chan)->chan->visdn_chan.name,			\
			(chan)->direction == RX ? "RX" : "TX",		\
			## arg)
#else
#define hfc_debug_chan(chan, dbglevel, format, arg...) do {} while (0)
#define hfc_debug_schan(schan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pcidev->dev.bus->name,		\
		(chan)->port->card->pcidev->dev.bus_id,			\
		(chan)->visdn_chan.name,				\
		## arg)

#define hfc_msg_schan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st:"							\
		"chan[%s,%s] "						\
		format,							\
		(chan)->port->card->pcidev->dev.bus->name,		\
		(chan)->chan->port->card->pcidev->dev.bus_id,		\
		(chan)->chan->chan->visdn_chan.name,			\
		(chan)->direction == RX ? "RX" : "TX",			\
		## arg)

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int bits = hfc_SQ_REC_BITS(hfc_inb(card, hfc_SQ_REC));

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

	hfc_outb(card, hfc_SQ_SEND, hfc_SQ_SEND_BITS(value));

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
	hfc_st_port_update_sctrl(port),
	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);


static int hfc_chan_sysfs_create_files_DB(
	struct hfc_chan_duplex *chan)
{
	return 0;
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


static void hfc_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_chan_release()\n");

	// FIXME
}

static int hfc_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int err;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->status != HFC_CHAN_STATUS_FREE) {
		hfc_debug_chan(chan, 1, "open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->visdn_chan.leg_b.framing == VISDN_LEG_FRAMING_NONE) {
	} else if (chan->visdn_chan.leg_b.framing == VISDN_LEG_FRAMING_HDLC) {
	} else {
		hfc_debug_chan(chan, 1,
			"open failed: unsupported l1_proto %d\n",
			chan->visdn_chan.leg_b.framing);
		err = -EINVAL;
		goto err_invalid_l1_proto;
	}

	switch(chan->id) {
	case D:
		chan->rx.fifo = &card->fifos[D][RX];
		chan->rx.fifo->connected_chan = &chan->rx;

		chan->tx.fifo = &card->fifos[D][TX];
		chan->tx.fifo->connected_chan = &chan->tx;

		chan->port->card->regs.mst_emod &= ~hfc_MST_EMOD_D_MASK;
		chan->port->card->regs.mst_emod |=
			hfc_MST_EMOD_D_HFC_from_ST |
			hfc_MST_EMOD_D_ST_from_HFC |
			hfc_MST_EMOD_D_GCI_from_HFC;

		hfc_outb(card, hfc_MST_EMOD, chan->port->card->regs.mst_emod);

		chan->port->card->regs.fifo_en |= hfc_FIFO_EN_D;
		chan->port->card->regs.m1 |=
			hfc_INT_M1_DREC | hfc_INT_M1_DTRANS;
	break;

	case B1:
		chan->rx.fifo = &card->fifos[B1][RX];
		chan->rx.fifo->connected_chan = &chan->rx;

		chan->tx.fifo = &card->fifos[B1][TX];
		chan->tx.fifo->connected_chan = &chan->tx;

		if (chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_NONE)
			chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB1;
		else if (chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_HDLC)
			chan->port->card->regs.ctmt &= ~hfc_CTMT_TRANSB1;

		chan->port->card->regs.connect &= hfc_CONNECT_B1_MASK;
		chan->port->card->regs.connect |=
			hfc_CONNECT_B1_HFC_from_ST |
			hfc_CONNECT_B1_ST_from_HFC |
			hfc_CONNECT_B1_GCI_from_HFC;

		chan->port->card->regs.fifo_en |= hfc_FIFO_EN_B1;
		chan->port->card->regs.m1 |=
			hfc_INT_M1_B1REC | hfc_INT_M1_B1TRANS;
	break;

	case B2:
		chan->rx.fifo = &card->fifos[B2][RX];
		chan->rx.fifo->connected_chan = &chan->rx;

		chan->tx.fifo = &card->fifos[B2][TX];
		chan->tx.fifo->connected_chan = &chan->tx;

		if (chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_NONE)
			chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB2;
		else if (chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_HDLC)
			chan->port->card->regs.ctmt &= ~hfc_CTMT_TRANSB2;

		chan->port->card->regs.connect &= hfc_CONNECT_B2_MASK;
		chan->port->card->regs.connect |=
			hfc_CONNECT_B2_HFC_from_ST |
			hfc_CONNECT_B2_ST_from_HFC |
			hfc_CONNECT_B2_GCI_from_HFC;

		chan->port->card->regs.m1 |=
			hfc_INT_M1_B2REC | hfc_INT_M1_B2TRANS;
		chan->port->card->regs.fifo_en |= hfc_FIFO_EN_B2;
	break;

	case E:
		if (port->chans[B2].status == HFC_CHAN_STATUS_FREE) {
			port->chans[B2].status = HFC_CHAN_STATUS_OPEN_E_AUX;

			chan->rx.fifo = &card->fifos[B2][RX];
			chan->rx.fifo->connected_chan = &chan->rx;

			chan->port->card->regs.connect &= hfc_CONNECT_B2_MASK;
			chan->port->card->regs.connect |=
				hfc_CONNECT_B2_HFC_from_ST |
				hfc_CONNECT_B2_ST_from_HFC |
				hfc_CONNECT_B2_GCI_from_HFC;

			chan->port->card->regs.m1 |= hfc_INT_M1_B2REC;
			chan->port->card->regs.fifo_en |= hfc_FIFO_EN_B2;
			chan->port->card->regs.ctmt &= ~hfc_CTMT_TRANSB2;

		} else if(port->chans[B1].status == HFC_CHAN_STATUS_FREE) {

			// We should switch B1/B2

			hfc_debug_chan(chan, 1, "B2 channel busy\n");

			err = -EBUSY;
			goto err_busy;

		} else {
			hfc_debug_chan(chan, 1,
				"No B channel available for E AUX\n");

			err = -EBUSY;
			goto err_busy;
		}

		card->regs.trm |= hfc_TRM_ECHO;
	break;

	default:
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	if (chan->rx.fifo) {
		hfc_fifo_reset(chan->rx.fifo);

		hfc_fifo_set_bit_order(
			chan->rx.fifo,
			chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_HDLC ? 1 : 0);
	}

	if (chan->tx.fifo) {
		hfc_fifo_reset(chan->tx.fifo);

		hfc_fifo_set_bit_order(
			chan->tx.fifo,
			chan->visdn_chan.leg_b.framing ==
				VISDN_LEG_FRAMING_HDLC ? 1 : 0);
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);
	hfc_outb(card, hfc_CONNECT, chan->port->card->regs.connect);
	hfc_outb(card, hfc_CTMT, chan->port->card->regs.ctmt);
	hfc_outb(card, hfc_TRM, chan->port->card->regs.trm);

	hfc_st_port_update_sctrl(chan->port);
	hfc_st_port_update_sctrl_r(chan->port);

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_msg_chan(chan, KERN_INFO, "channel opened.\n");

	return 0;

err_busy:
	chan->status = HFC_CHAN_STATUS_FREE;
err_invalid_l1_proto:
err_invalid_chan:
err_channel_busy:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static int hfc_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	chan->status = HFC_CHAN_STATUS_FREE;

	if (chan->id == B1 || chan->id == B2) {
		if (chan->id == B1) {
			chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_B1;
			chan->port->card->regs.m1 &=
				~(hfc_INT_M1_B1REC | hfc_INT_M1_B1TRANS);
		} else if (chan->id == B2) {
			chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_B2;
			chan->port->card->regs.m1 &=
				~(hfc_INT_M1_B2REC | hfc_INT_M1_B2TRANS);
		}

		hfc_st_port_update_sctrl(chan->port);
		hfc_st_port_update_sctrl_r(chan->port);
	} else if (chan->id == D) {
		chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_DTX;
		chan->port->card->regs.m1 &=
				~(hfc_INT_M1_DREC | hfc_INT_M1_DTRANS);
	} else if (chan->id == E) {
		port->chans[B2].status = HFC_CHAN_STATUS_FREE;
		card->regs.trm &= ~hfc_TRM_ECHO;
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);
	hfc_outb(card, hfc_TRM, chan->port->card->regs.trm);

	if (chan->rx.fifo) {
		hfc_fifo_reset(chan->rx.fifo);

		chan->rx.fifo->connected_chan = NULL;
		chan->rx.fifo = NULL;
	}

	if (chan->tx.fifo) {
		hfc_fifo_reset(chan->tx.fifo);

		chan->tx.fifo->connected_chan = NULL;
		chan->tx.fifo = NULL;
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_chan(chan, 1, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
}

static int hfc_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	hfc_st_port_check_l1_up(chan->port);

	struct hfc_fifo *fifo = chan->tx.fifo;

	if (!hfc_fifo_free_frames(fifo)) {
		hfc_debug_chan(chan, 3, "TX FIFO frames full, throttling\n");

		visdn_leg_stop_queue(&chan->visdn_chan.leg_b);

		visdn_leg_tx_error(&chan->visdn_chan.leg_b,
			VISDN_TX_ERROR_FIFO_FULL);

		goto err_no_free_frames;
	}

	if (hfc_fifo_free_tx(fifo) < skb->len) {
		hfc_debug_chan(chan, 3, "TX FIFO full, throttling\n");

		visdn_leg_stop_queue(&chan->visdn_chan.leg_b);

		visdn_leg_tx_error(&chan->visdn_chan.leg_b,
			VISDN_TX_ERROR_FIFO_FULL);

		goto err_no_free_tx;
	}

#ifdef DEBUG_CODE
	if (debug_level == 3) {
		hfc_debug_fifo(fifo, 3, "TX len %2d: ", skb->len);

	} else if (debug_level >= 4) {
		hfc_debug_fifo(fifo, 4,
			"TX (f1=%02x, f2=%02x, z1=%04x z2=%04x) len %2d: ",
			*fifo->f1, *fifo->f2,
			*Z1_F1(fifo), *Z2_F1(fifo),
			skb->len);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<skb->len; i++)
			printk("%02x",((u8 *)skb->data)[i]);

		printk("\n");
	}
#endif

	hfc_fifo_mem_write(fifo, skb->data, skb->len);

	// Move Z1 and jump to next frame
	u16 newz1 = Z_inc(fifo, *Z1_F1(fifo), skb->len);
	*Z1_F1(fifo) = newz1;
	*fifo->f1 = F_inc(fifo, *fifo->f1, 1);
	*Z1_F1(fifo) = newz1;

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	return NETDEV_TX_OK;

err_no_free_tx:
err_no_free_frames:

	hfc_card_unlock(card);

	return NETDEV_TX_BUSY;
}

static ssize_t hfc_chan_read(
	struct visdn_leg *visdn_leg,
	void *buf,
	size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	int copied_octets = hfc_fifo_used_rx(chan->rx.fifo);
	if (copied_octets > count)
		copied_octets = count;

	 hfc_fifo_mem_read(chan->rx.fifo,
				buf, copied_octets);

	*Z2_F2(chan->rx.fifo) = Z_inc(chan->rx.fifo,
					*Z2_F2(chan->rx.fifo),
					copied_octets);

	hfc_card_unlock(card);

	return copied_octets;
}

static ssize_t hfc_chan_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	int copied_octets = hfc_fifo_free_tx(chan->tx.fifo);
	if (copied_octets > count)
		copied_octets = count;

	hfc_fifo_mem_write(chan->tx.fifo, buf, copied_octets);

	*Z1_F1(chan->tx.fifo) = Z_inc(chan->tx.fifo,
					*Z1_F1(chan->tx.fifo),
					count);

	hfc_card_unlock(card);

	return copied_octets;
}

static int hfc_chan_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "connecting to %06d\n",
		visdn_leg2->chan->id);

	return 0;
}

static void hfc_chan_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "disconnected from %06d\n",
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops hfc_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_chan_release,
	.open		= hfc_chan_open,
	.close		= hfc_chan_close,
};

static struct visdn_leg_ops hfc_leg_ops =
{
	.owner		= THIS_MODULE,

	.connect	= hfc_chan_connect,
	.disconnect	= hfc_chan_disconnect,

	.frame_xmit	= hfc_chan_frame_xmit,

	.read		= hfc_chan_read,
	.write		= hfc_chan_write,
};

void hfc_chan_init(
	struct hfc_chan_duplex *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	const int bitrates[],
	int bitrates_cnt)
{
	chan->port = port;
	chan->status = HFC_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	chan->rx.chan = chan;
	chan->rx.direction = RX;
	chan->rx.fifo = NULL;

	chan->tx.chan = chan;
	chan->tx.direction = TX;
	chan->tx.fifo = NULL;

	visdn_chan_init(&chan->visdn_chan);
	chan->visdn_chan.ops = &hfc_chan_ops;
	chan->visdn_chan.chan_class = NULL;
	chan->visdn_chan.port = &port->visdn_port;

	chan->visdn_chan.leg_a.cxc = NULL;
	chan->visdn_chan.leg_a.ops = NULL;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_b.ops = &hfc_leg_ops;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE |
						VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_b.mtu = 0;

	strncpy(chan->visdn_chan.name, name, sizeof(chan->visdn_chan.name));
	chan->visdn_chan.driver_data = chan;
}

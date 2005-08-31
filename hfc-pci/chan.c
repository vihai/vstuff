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

#include <visdn.h>

#include "chan.h"
#include "card.h"
#include "st_port.h"
#include "fifo.h"
#include "fifo_inline.h"

static int hfc_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	int err;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	if (chan->status != HFC_CHAN_STATUS_FREE) {
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->visdn_chan.framing_current ==
				VISDN_CHAN_FRAMING_TRANS) {
		chan->status = HFC_CHAN_STATUS_OPEN_TRANS;
	} else if (chan->visdn_chan.framing_current ==
				VISDN_CHAN_FRAMING_HDLC) {
		chan->status = HFC_CHAN_STATUS_OPEN_HDLC;
	} else {
		err = -EINVAL;
		goto err_invalid_framing;
	}

	if (chan->id == D) {
		chan->port->card->regs.mst_emod &= ~hfc_MST_EMOD_D_MASK;
		chan->port->card->regs.mst_emod |=
			hfc_MST_EMOD_D_HFC_from_ST |
			hfc_MST_EMOD_D_ST_from_HFC |
			hfc_MST_EMOD_D_GCI_from_HFC;

		hfc_outb(card, hfc_MST_EMOD, chan->port->card->regs.mst_emod);

		chan->port->card->regs.fifo_en |= hfc_FIFO_EN_D;
		chan->port->card->regs.m1 |= hfc_INT_M1_DREC | hfc_INT_M1_DTRANS;
	} else if (chan->id == B1 || chan->id == B2) {
		if (chan->id == B1) {
			if (chan->visdn_chan.framing_current ==
					VISDN_CHAN_FRAMING_TRANS) {
				chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB1;
			} else if (chan->visdn_chan.framing_current ==
					VISDN_CHAN_FRAMING_HDLC) {
				chan->port->card->regs.ctmt &= ~hfc_CTMT_TRANSB1;
			}

			chan->port->card->regs.connect &= hfc_CONNECT_B1_MASK;
			chan->port->card->regs.connect |=
				hfc_CONNECT_B1_HFC_from_ST |
				hfc_CONNECT_B1_ST_from_HFC |
				hfc_CONNECT_B1_GCI_from_HFC;

			chan->port->card->regs.fifo_en |= hfc_FIFO_EN_B1;
			chan->port->card->regs.m1 |= hfc_INT_M1_B1REC | hfc_INT_M1_B1TRANS;
		} else {
			if (chan->visdn_chan.framing_current ==
					VISDN_CHAN_FRAMING_TRANS) {
				chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB2;
			} else if (chan->visdn_chan.framing_current ==
					VISDN_CHAN_FRAMING_HDLC) {
				chan->port->card->regs.ctmt &= ~hfc_CTMT_TRANSB2;
			}

			chan->port->card->regs.connect &= hfc_CONNECT_B2_MASK;
			chan->port->card->regs.connect |=
				hfc_CONNECT_B2_HFC_from_ST |
				hfc_CONNECT_B2_ST_from_HFC |
				hfc_CONNECT_B2_GCI_from_HFC;

			chan->port->card->regs.m1 |= hfc_INT_M1_B2REC | hfc_INT_M1_B2TRANS;
			chan->port->card->regs.fifo_en |= hfc_FIFO_EN_B2;
		}

		hfc_outb(card, hfc_CONNECT, chan->port->card->regs.connect);
		hfc_outb(card, hfc_CTMT, chan->port->card->regs.ctmt);
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);

	hfc_fifo_set_bit_order(
		chan->rx.fifo,
		chan->visdn_chan.bitorder_current ==
			VISDN_CHAN_BITORDER_MSB);
	hfc_fifo_set_bit_order(
		chan->tx.fifo,
		chan->visdn_chan.bitorder_current ==
			VISDN_CHAN_BITORDER_MSB);

	hfc_st_port_update_sctrl(chan->port);
	hfc_st_port_update_sctrl_r(chan->port);

	hfc_card_unlock(card);

	hfc_msg_chan(chan, KERN_INFO, "channel opened.\n");

	return 0;

/*	spin_lock_irqsave(&card->lock, flags);
	chan->status = HFC_CHAN_STATUS_FREE;
	spin_unlock_irqrestore(&card->lock, flags);*/
err_invalid_framing:
err_channel_busy:

	hfc_card_unlock(card);

	return err;
}

static int hfc_chan_close(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	chan->status = HFC_CHAN_STATUS_FREE;

	if (chan->id == B1 || chan->id == B2) {
		if (chan->id == B1) {
			chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_B1;
			chan->port->card->regs.m1 &= ~(hfc_INT_M1_B1REC | hfc_INT_M1_B1TRANS);
		} else if (chan->id == B2) {
			chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_B2;
			chan->port->card->regs.m1 &= ~(hfc_INT_M1_B2REC | hfc_INT_M1_B2TRANS);
		}

		hfc_st_port_update_sctrl(chan->port);
		hfc_st_port_update_sctrl_r(chan->port);
	} else if (chan->id == D) {
		chan->port->card->regs.fifo_en &= ~hfc_FIFO_EN_DTX;
		chan->port->card->regs.m1 &= ~(hfc_INT_M1_DREC | hfc_INT_M1_DTRANS);
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);

	hfc_card_unlock(card);

	hfc_msg_chan(chan, KERN_INFO, "channel closed.\n");

	return 0;
}

static int hfc_chan_frame_xmit(
	struct visdn_chan *visdn_chan,
	struct sk_buff *skb)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	// Should we lock at all?
	if (hfc_card_trylock(card)) {
		// Mmmh... the card is locked and we may be in interrupt
		// context. We must defer the transmission.

		return NETDEV_TX_LOCKED;
	}

	hfc_st_port_check_l1_up(chan->port);

/*
	if (!hfc_fifo_free_frames(chan->tx.fifo)) {
		hfc_debug_chan(chan, 3, "TX FIFO frames full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_frames;
	}

	if (hfc_fifo_free_tx(chan->tx.fifo) < skb->len) {
		hfc_debug_chan(chan, 3, "TX FIFO full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_tx;
	}*/

	hfc_fifo_put_frame(chan->tx.fifo, skb->data, skb->len);

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	return NETDEV_TX_OK;

//err_no_free_tx:
//err_no_free_frames:

	hfc_card_unlock(card);

	return NETDEV_TX_BUSY;
}

static struct net_device_stats *hfc_chan_get_stats(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_card *card = chan->port->card;

	return &chan->net_device_stats;
}

/*

TO ENABLE SNIFFING, PICK UP A FREE CHANNEL
IF CHANNEL IS B2 WE'RE OK
IF CHANNEL IS B1 SWITCH B1/B2 WITH SCTRL_E AND EXCHANGE FIFOS

static void hfc_set_multicast_list(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

        if(netdev->flags & IFF_PROMISC && !port->echo_enabled) {
		if (port->nt_mode) {
			hfc_msg_port(port, KERN_INFO,
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

		hfc_msg_port(port, KERN_INFO,
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

		hfc_msg_port(port, KERN_INFO,
			"left promiscuous mode.\n");
	}

	spin_unlock_irqrestore(&card->lock, flags);

//	hfc_update_fifo_state(card);
}
*/

static ssize_t hfc_chan_samples_read(
	struct visdn_chan *visdn_chan,
	char __user *buf,
	size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	int err;

	// Should we lock?
	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	int copied_octets = hfc_fifo_used_rx(chan->rx.fifo);
	if (copied_octets > count)
		copied_octets = count;

	err = hfc_fifo_mem_read_user(chan->rx.fifo, buf, copied_octets);
	if (err < 0)
		goto err_fifo_mem_read_user;

	*Z2_F2(chan->rx.fifo) = Z_inc(chan->rx.fifo,
					*Z2_F2(chan->rx.fifo),
					copied_octets);

	hfc_card_unlock(card);

	return copied_octets;

err_fifo_mem_read_user:

	hfc_card_unlock(card);

	return err;
}

static ssize_t hfc_chan_samples_write(
	struct visdn_chan *visdn_chan,
	const char __user *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	int err = 0;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	int copied_octets = hfc_fifo_free_tx(chan->tx.fifo);
	if (copied_octets > count)
		copied_octets = count;

	err = hfc_fifo_mem_write_user(chan->tx.fifo, buf, copied_octets);
	if (err < 0)
		goto err_fifo_mem_write_user;

	*Z1_F1(chan->tx.fifo) = Z_inc(chan->tx.fifo,
					*Z1_F1(chan->tx.fifo),
					count);

	hfc_card_unlock(card);

	return copied_octets;

err_fifo_mem_write_user:

	hfc_card_unlock(card);

	return err;
}

static int hfc_chan_do_ioctl(struct visdn_chan *visdn_chan,
	struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static int hfc_bridge(
	struct hfc_chan_duplex *chan,
	struct hfc_chan_duplex *chan2)
{
	return -EOPNOTSUPP;
}

static int hfc_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan->connected_chan)
		return -EBUSY;

	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_chan_duplex *chan2 = visdn_chan2->priv;
	struct hfc_card *card = chan->port->card;

	hfc_debug_card(card, 2, "Connecting chan %s to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	if (chan->id != B1 && chan->id != B2) {
		hfc_msg(KERN_ERR, "Cannot connect %s to %s\n",
			chan->name, to_chan_duplex(visdn_chan2)->name);
		return -EINVAL;
	}

/*	if (visdn_chan->device.parent->parent ==
			visdn_chan2->device.parent->parent) {
		printk(KERN_DEBUG "Both channels belong to the me,"
			" attempting private bridge\n");

		return hfc_bridge(chan, chan2);
	}*/

	return VISDN_CONNECT_OK;
}

static int hfc_chan_disconnect(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan->connected_chan)
		return 0;

printk(KERN_INFO "hfc-4s chan %s disconnecting from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	return 0;
}

struct visdn_chan_ops hfc_chan_ops = {
	.open		= hfc_chan_open,
	.close		= hfc_chan_close,
	.frame_xmit	= hfc_chan_frame_xmit,
	.get_stats	= hfc_chan_get_stats,
	.do_ioctl	= hfc_chan_do_ioctl,

	.connect_to	= hfc_chan_connect_to,
	.disconnect	= hfc_chan_disconnect,

	.samples_read	= hfc_chan_samples_read,
	.samples_write	= hfc_chan_samples_write,
};

void hfc_chan_init(
	struct hfc_chan_duplex *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	int hw_index,
	int speed,
	int role,
	int roles,
	struct hfc_fifo *fifo_rx,
	struct hfc_fifo *fifo_tx)
{
	chan->port = port;
	chan->name = name;
	chan->status = HFC_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	chan->rx.chan = chan;
	chan->rx.direction = RX;
	chan->rx.fifo = fifo_rx;
	if (fifo_rx)
		fifo_rx->connected_chan = &chan->rx;

	chan->tx.chan = chan;
	chan->tx.direction = TX;
	chan->tx.fifo = fifo_tx;
	if (fifo_tx)
		fifo_tx->connected_chan = &chan->tx;

	visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);
	chan->visdn_chan.priv = chan;
	chan->visdn_chan.speed = speed;
	chan->visdn_chan.role = role;
	chan->visdn_chan.roles = roles;
	chan->visdn_chan.flags = 0;

	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_TRANS |
					     VISDN_CHAN_FRAMING_HDLC;
	chan->visdn_chan.framing_preferred = 0;

	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB |
					      VISDN_CHAN_BITORDER_MSB;
	chan->visdn_chan.bitorder_preferred = 0;
}


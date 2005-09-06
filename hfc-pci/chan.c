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

static void hfc_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_chan_release()\n");

	// FIXME
}

static int hfc_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int err;
	int res = VISDN_CHAN_OPEN_OK;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	if (hfc_card_lock_interruptible(card)) {
		err = -ERESTARTSYS;
		goto err_card_lock;
	}

	if (chan->status != HFC_CHAN_STATUS_FREE) {
		hfc_debug_chan(chan, 1, "open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->visdn_chan.pars.framing ==
				VISDN_CHAN_FRAMING_TRANS) {
		chan->status = HFC_CHAN_STATUS_OPEN_TRANS;
	} else if (chan->visdn_chan.pars.framing ==
				VISDN_CHAN_FRAMING_HDLC) {
		chan->status = HFC_CHAN_STATUS_OPEN_HDLC;
	} else {
		hfc_debug_chan(chan, 1,
			"open failed: unsupported framing %d\n",
			chan->visdn_chan.pars.framing);
		err = -EINVAL;
		goto err_invalid_framing;
	}

	if (chan->id == D) {
		chan->rx.fifo = &card->fifos[D][RX];
		chan->rx.fifo->connected_chan = &chan->rx;

		chan->tx.fifo = &card->fifos[D][TX];
		chan->tx.fifo->connected_chan = &chan->tx;

		chan->visdn_chan.max_mtu = chan->tx.fifo->fifo_size;

		res = VISDN_CHAN_OPEN_RENEGOTIATE;

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
			chan->rx.fifo = &card->fifos[B1][RX];
			chan->rx.fifo->connected_chan = &chan->rx;

			chan->tx.fifo = &card->fifos[B1][TX];
			chan->tx.fifo->connected_chan = &chan->tx;

			chan->visdn_chan.max_mtu = chan->tx.fifo->fifo_size;

			res = VISDN_CHAN_OPEN_RENEGOTIATE;

			if (chan->visdn_chan.pars.framing ==
					VISDN_CHAN_FRAMING_TRANS) {
				chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB1;
			} else if (chan->visdn_chan.pars.framing ==
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
			chan->rx.fifo = &card->fifos[B2][RX];
			chan->rx.fifo->connected_chan = &chan->rx;

			chan->tx.fifo = &card->fifos[B2][TX];
			chan->tx.fifo->connected_chan = &chan->tx;

			chan->visdn_chan.max_mtu = chan->tx.fifo->fifo_size;

			res = VISDN_CHAN_OPEN_RENEGOTIATE;

			if (chan->visdn_chan.pars.framing ==
					VISDN_CHAN_FRAMING_TRANS) {
				chan->port->card->regs.ctmt |= hfc_CTMT_TRANSB2;
			} else if (chan->visdn_chan.pars.framing ==
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
	} else if (chan->id == E) {

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
			hfc_debug_chan(chan, 1, "No B channel available for E AUX\n");

			err = -EBUSY;
			goto err_busy;
		}

		card->regs.trm |= hfc_TRM_ECHO;

	} else {
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);
	hfc_outb(card, hfc_CONNECT, chan->port->card->regs.connect);
	hfc_outb(card, hfc_CTMT, chan->port->card->regs.ctmt);
	hfc_outb(card, hfc_TRM, chan->port->card->regs.trm);

	if (chan->rx.fifo) {
		hfc_fifo_set_bit_order(
			chan->rx.fifo,
			chan->visdn_chan.pars.bitorder ==
				VISDN_CHAN_BITORDER_MSB);
	}

	if (chan->tx.fifo) {
		hfc_fifo_set_bit_order(
			chan->tx.fifo,
			chan->visdn_chan.pars.bitorder ==
				VISDN_CHAN_BITORDER_MSB);
	}

	hfc_st_port_update_sctrl(chan->port);
	hfc_st_port_update_sctrl_r(chan->port);

	memset(&chan->stats, 0x00, sizeof(chan->stats));

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_msg_chan(chan, KERN_INFO, "channel opened.\n");

	return res;

err_busy:
	chan->status = HFC_CHAN_STATUS_FREE;
err_invalid_framing:
err_invalid_chan:
err_channel_busy:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);
err_card_lock:

	hfc_debug_chan(chan, 1, "Open failed: %d\n", err);

	return err;
}

static int hfc_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_card *card = chan->port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	if (hfc_card_lock_interruptible(card)) {
		err = -ERESTARTSYS;
		goto err_card_lock;
	}

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
	} else if (chan->id == E) {
		card->regs.trm &= ~hfc_TRM_ECHO;
	}

	hfc_outb(card, hfc_FIFO_EN, chan->port->card->regs.fifo_en);
	hfc_outb(card, hfc_INT_M1, chan->port->card->regs.m1);
	hfc_outb(card, hfc_TRM, chan->port->card->regs.trm);

	if (chan->rx.fifo) {
		chan->rx.fifo->connected_chan = NULL;
		chan->rx.fifo = NULL;
	}

	if (chan->tx.fifo) {
		chan->tx.fifo->connected_chan = NULL;
		chan->tx.fifo = NULL;
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_msg_chan(chan, KERN_INFO, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
err_card_lock:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
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

	struct hfc_fifo *fifo = chan->tx.fifo;

	if (!hfc_fifo_free_frames(fifo)) {
		hfc_debug_chan(chan, 3, "TX FIFO frames full, throttling\n");

		visdn_pass_stop_queue(visdn_chan);

		chan->stats.tx_errors++;
		chan->stats.tx_fifo_errors++;

		goto err_no_free_frames;
	}

	if (hfc_fifo_free_tx(fifo) < skb->len) {
		hfc_debug_chan(chan, 3, "TX FIFO full, throttling\n");

		visdn_pass_stop_queue(visdn_chan);

		goto err_no_free_tx;
	}

	hfc_fifo_mem_write(fifo, skb->data, skb->len);

	// Move Z1 and jump to next frame
	u16 newz1 = Z_inc(fifo, *Z1_F1(fifo), skb->len);
	*Z1_F1(fifo) = newz1;
	*fifo->f1 = F_inc(fifo, *fifo->f1, 1);
	*Z1_F1(fifo) = newz1;

	chan->stats.tx_packets++;
	chan->stats.tx_bytes += skb->len + 2;

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	return NETDEV_TX_OK;

err_no_free_tx:
err_no_free_frames:

	hfc_card_unlock(card);

	return NETDEV_TX_BUSY;
}

static struct net_device_stats *hfc_chan_get_stats(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;

	return &chan->stats;
}

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

	chan->stats.rx_bytes += copied_octets;

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

	chan->stats.rx_bytes += copied_octets;

	hfc_card_unlock(card);

	return copied_octets;

err_fifo_mem_write_user:

	hfc_card_unlock(card);

	return err;
}

static int hfc_bridge(
	struct hfc_chan_duplex *chan,
	struct hfc_chan_duplex *chan2)
{
/*	if (chan->id != B1 && chan->id != B2) {
		hfc_msg_chan(chan, KERN_ERR, "Cannot connect to %s\n",
			chan2->name);

		return -EINVAL;
	}*/

	return -ENOTSUPP;
}

static int hfc_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	hfc_debug_chan(chan, 2, "connecting to %s\n",
		visdn_chan2->device.bus_id);

/*	if (visdn_chan->device.parent->parent ==
			visdn_chan2->device.parent->parent) {
		printk(KERN_DEBUG "Both channels belong to the me,"
			" attempting private bridge\n");

		struct hfc_chan_duplex *chan2 = to_chan_duplex(visdn_chan2);

		return hfc_bridge(chan, chan2);
	}*/

	return VISDN_CONNECT_OK;
}

static int hfc_chan_disconnect(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	hfc_debug_chan(chan, 2, "hfc-4s chan %s disconnected\n",
		visdn_chan->device.bus_id);

	return 0;
}

static int hfc_chan_update_parameters(
        struct visdn_chan *chan,
        struct visdn_chan_pars *pars)
{
	memcpy(&chan->pars, pars, sizeof(chan->pars));

	return 0;
}

struct visdn_chan_ops hfc_chan_ops = {
	.owner			= THIS_MODULE,
	.release		= hfc_chan_release,
	.open			= hfc_chan_open,
	.close			= hfc_chan_close,
	.frame_xmit		= hfc_chan_frame_xmit,
	.get_stats		= hfc_chan_get_stats,

	.connect_to		= hfc_chan_connect_to,
	.disconnect		= hfc_chan_disconnect,

	.update_parameters	= hfc_chan_update_parameters,

	.samples_read		= hfc_chan_samples_read,
	.samples_write		= hfc_chan_samples_write,
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
	chan->name = name;
	chan->status = HFC_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	chan->rx.chan = chan;
	chan->rx.direction = RX;
	chan->rx.fifo = NULL;

	chan->tx.chan = chan;
	chan->tx.direction = TX;
	chan->tx.fifo = NULL;

	visdn_chan_init(&chan->visdn_chan, &hfc_chan_ops);

	chan->visdn_chan.priv = chan;
	chan->visdn_chan.autoopen = TRUE;
	chan->visdn_chan.max_mtu = 0; // We'll set it after opening the port
	chan->visdn_chan.bitrate_selection = VISDN_CHAN_BITRATE_SELECTION_LIST;
	memcpy(chan->visdn_chan.bitrates, bitrates, sizeof(bitrates));
	chan->visdn_chan.bitrates_cnt = bitrates_cnt;
	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_TRANS |
					     VISDN_CHAN_FRAMING_HDLC;
	chan->visdn_chan.framing_preferred = 0;
	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB |
					      VISDN_CHAN_BITORDER_MSB;
	chan->visdn_chan.bitorder_preferred = 0;
}


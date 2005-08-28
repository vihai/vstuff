#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/delay.h>

#include <visdn.h>

#include "chan.h"
#include "card.h"
#include "st_port.h"
#include "st_port_inline.h"
#include "pcm_port_inline.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "fsm.h"

void hfc_chan_disable(struct hfc_chan_duplex *chan)
{
}

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

	struct hfc_fifo *fifo_rx;
	fifo_rx = hfc_allocate_fifo(card, RX);
	if (!fifo_rx) {
		err = -ENOMEM;
		goto err_allocate_fifo_rx;
	}

	if (chan->id != E) {
		struct hfc_fifo *fifo_tx;
		fifo_tx = hfc_allocate_fifo(card, TX);
		if (!fifo_tx) {
			err = -ENOMEM;
			goto err_allocate_fifo_tx;
		}

		chan->tx.fifo = fifo_tx;
		fifo_tx->connected_chan = &chan->tx;
	}

	chan->rx.fifo = fifo_rx;
	fifo_rx->connected_chan = &chan->rx;

	hfc_upload_fsm(card);

	// ------------- RX -----------------------------

	hfc_fifo_set_bit_order(
		chan->rx.fifo,
		chan->visdn_chan.bitorder_current ==
			VISDN_CHAN_BITORDER_MSB);

	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);

	if (chan->visdn_chan.framing_current ==
			VISDN_CHAN_FRAMING_TRANS) {

		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	} else if (chan->visdn_chan.framing_current ==
			VISDN_CHAN_FRAMING_HDLC) {

		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_IFF|
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);
	}

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_IRQ);

	// ------------- TX -----------------------------

	if (chan->tx.fifo) {
		hfc_fifo_set_bit_order(
			chan->tx.fifo,
			chan->visdn_chan.bitorder_current ==
				VISDN_CHAN_BITORDER_MSB);

		hfc_fifo_select(chan->tx.fifo);
		hfc_fifo_reset(chan->tx.fifo);

		if (chan->visdn_chan.framing_current ==
				VISDN_CHAN_FRAMING_TRANS) {

			hfc_outb(card, hfc_A_CON_HDLC,
				hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
				hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
				hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

		} else if (chan->visdn_chan.framing_current ==
				VISDN_CHAN_FRAMING_HDLC) {

			hfc_outb(card, hfc_A_CON_HDLC,
				hfc_A_CON_HDCL_V_IFF|
				hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
				hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
				hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);
		}

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_IRQ);
	}

	// Enable channel in the port
	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl_0(chan->port);
	hfc_st_port_update_st_ctrl_2(chan->port);

	hfc_card_unlock(card);

	hfc_msg_chan(chan, KERN_INFO, "channel opened.\n");

	return 0;

	hfc_deallocate_fifo(chan->tx.fifo);
err_allocate_fifo_tx:
	hfc_deallocate_fifo(chan->rx.fifo);
err_allocate_fifo_rx:
	chan->status = HFC_CHAN_STATUS_FREE;
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

	if (chan->rx.fifo) {
		hfc_deallocate_fifo(chan->rx.fifo);
		chan->rx.fifo->connected_chan = NULL;
		chan->rx.fifo = NULL;
	}

	if (chan->tx.fifo) {
		hfc_deallocate_fifo(chan->tx.fifo);
		chan->tx.fifo->connected_chan = NULL;
		chan->tx.fifo = NULL;
	}

	if (chan->rx.slot) {
		hfc_pcm_port_deallocate_slot(chan->rx.slot);
		chan->rx.slot->connected_chan = NULL;
		chan->rx.slot = NULL;
	}

	if (chan->tx.slot) {
		hfc_pcm_port_deallocate_slot(chan->tx.slot);
		chan->tx.slot->connected_chan = NULL;
		chan->tx.slot = NULL;
	}

	hfc_upload_fsm(card);

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

	if (hfc_card_trylock(card)) {
		// Mmmh... the card is locked and we may be in interrupt
		// context. We must defer the transmission.

		return 1;
	}
	
	hfc_st_port_select(chan->port);
	hfc_st_port_check_l1_up(chan->port);

	hfc_fifo_select(chan->tx.fifo);
	// hfc_fifo_select() updates F/Z cache, so,
	// size calculations are allowed

	if (!hfc_fifo_free_frames(chan->tx.fifo)) {
		hfc_debug_chan(chan, 3, "TX FIFO frames full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_frames;
	}

	if (hfc_fifo_free_tx(chan->tx.fifo) < skb->len) {
		hfc_debug_chan(chan, 3, "TX FIFO full, throttling\n");

		visdn_stop_queue(visdn_chan);

		goto err_no_free_tx;
	}

	hfc_fifo_put_frame(chan->tx.fifo, skb->data, skb->len);

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	return 0;

err_no_free_tx:
err_no_free_frames:

	hfc_card_unlock(card);

	return 0;
}

static struct net_device_stats *hfc_chan_get_stats(struct visdn_chan *visdn_chan)
{
	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_card *card = chan->card;

	return &chan->net_device_stats;
}

/*
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
	char __user *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;

	int err;

	// Avoid user specifying too big transfers
	if (count > 65536)
		count = 65536;

	// Allocate a big enough buffer
	u8 *buf2 = kmalloc(count, GFP_KERNEL);
	if (!buf2) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_refresh_fz_cache(chan->rx.fifo);

	int available_octets = hfc_fifo_used_rx(chan->rx.fifo);
	int copied_octets = 0;

	copied_octets = available_octets < count ? available_octets : count;

	// Read from FIFO in atomic context
	// Cannot read directly to user due to put_user sleeping
	// NO, now we can!!! FIXME!
	hfc_fifo_mem_read(chan->rx.fifo, buf2, copied_octets);

	hfc_card_unlock(card);

	err = copy_to_user(buf, buf2, copied_octets);
	if (err < 0)
		goto err_copy_to_user;

	kfree(buf2);

	return copied_octets;

err_copy_to_user:
	kfree(buf2);
err_kmalloc:

	return err;
}

static ssize_t hfc_chan_samples_write(
	struct visdn_chan *visdn_chan,
	const char __user *buf, size_t count)
{
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_card *card = chan->port->card;
	int err = 0;

	__u8 buf2[256]; // FIXME TODO

	// Umpf... we need an intermediate buffer... we need to disable interrupts
	// for the whole time since we must ensure that noone selects another FIFO
	// in the meantime, so we may not directly copy from FIFO to user.
	// There is for sure a better solution :)

	int copied_octets = count;
	if (copied_octets > sizeof(buf2))
		copied_octets = sizeof(buf2);

	err = copy_from_user(buf2, buf, copied_octets);
	if (err < 0)
		goto err_copy_to_user;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_refresh_fz_cache(chan->tx.fifo);

	int available_octets = hfc_fifo_free_tx(chan->tx.fifo);
	if (copied_octets > available_octets)
		copied_octets = available_octets;

	hfc_fifo_put(chan->rx.fifo, buf2, copied_octets);

	hfc_card_unlock(card);

	return copied_octets;

err_copy_to_user:

	return err;
}

static int hfc_chan_do_ioctl(struct visdn_chan *visdn_chan,
	struct ifreq *ifr, int cmd)
{
//	struct hfc_chan_duplex *chan = visdn_chan->priv;
//	struct hfc_card *card = chan->port->card;

//	unsigned long flags;
/*

	switch (cmd) {
	case VISDN_SET_BEARER: {

	struct sb_setbearer sb;
	if (copy_from_user(&sb, ifr->ifr_data, sizeof(sb)))
		return -EFAULT;

hfc_msg_chan(chan, KERN_INFO, "VISDN_SET_BEARER %d %d\n", sb.sb_index, sb.sb_bearertype);

	struct hfc_chan_duplex *bchan;
	if (sb.sb_index == 0)
		bchan = &chan->port->chans[B1];
	else if (sb.sb_index == 1)
		bchan = &chan->port->chans[B2];
	else
		return -EINVAL;

	if (sb.sb_bearertype == VISDN_BT_VOICE) {
		
	} else if (sb.sb_bearertype == VISDN_BT_PPP) {

		b1_chan->status = open_ppp;

		spin_unlock_irqrestore(&card->lock, flags);

////////////////////////////
		b1_chan->ppp_chan.private = b1_chan;
		b1_chan->ppp_chan.ops = &hfc_ppp_ops;
		b1_chan->ppp_chan.mtu = 1000; //FIXME
		b1_chan->ppp_chan.hdrlen = 2;

		ppp_register_channel(&b1_chan->ppp_chan);
////////////////////////

hfc_msg_chan(chan, KERN_INFO,
	"PPPPPPPPPPPP: int %d unit %d\n",
	ppp_channel_index(&b1_chan->ppp_chan),
	ppp_unit_number(&b1_chan->ppp_chan));


	break;

	case VISDN_PPP_GET_CHAN:
hfc_msg_chan(chan, KERN_INFO, "VISDN_PPP_GET_CHAN:\n");

		put_user(ppp_channel_index(&bchan->ppp_chan),
			(int __user *)ifr->ifr_data);
	break;

	case VISDN_PPP_GET_UNIT:
hfc_msg_chan(chan, KERN_INFO, "VISDN_PPP_GET_UNIT:\n");

		put_user(ppp_unit_number(&bchan->ppp_chan),
			(int __user *)ifr->ifr_data);
	break;

	default:
		return -ENOIOCTLCMD;
	}
*/

	return 0;
}

static int hfc_bridge(
	struct hfc_chan_duplex *chan,
	struct hfc_chan_duplex *chan2)
{
	struct hfc_card *card = chan->port->card;
	int err;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

	struct hfc_fifo *fifo_1_rx = hfc_allocate_fifo(card, RX);
	if (!fifo_1_rx) {
		err = -EBUSY;
		goto err_allocate_fifo_1_rx;
	}

	struct hfc_fifo *fifo_1_tx = hfc_allocate_fifo(card, TX);
	if (!fifo_1_tx) {
		err = -EBUSY;
		goto err_allocate_fifo_1_tx;
	}

	struct hfc_fifo *fifo_2_rx = hfc_allocate_fifo(card, RX);
	if (!fifo_2_rx) {
		err = -EBUSY;
		goto err_allocate_fifo_2_rx;
	}

	struct hfc_fifo *fifo_2_tx = hfc_allocate_fifo(card, TX);
	if (!fifo_2_tx) {
		err = -EBUSY;
		goto err_allocate_fifo_2_tx;
	}

	struct hfc_pcm_slot *slot_1_rx;
	slot_1_rx = hfc_pcm_port_allocate_slot(&card->pcm_port, RX);
	if (!slot_1_rx) {
		err = -EBUSY;
		goto err_allocate_slot_1_rx;
	}

	struct hfc_pcm_slot *slot_1_tx;
	slot_1_tx = hfc_pcm_port_allocate_slot(&card->pcm_port, TX);
	if (!slot_1_tx) {
		err = -EBUSY;
		goto err_allocate_slot_1_tx;
	}

	 // Without = NULL gcc complains, why?
	struct hfc_pcm_slot *slot_2_rx = NULL;
	slot_2_rx = hfc_pcm_port_allocate_slot(&card->pcm_port, RX);
	if (!slot_2_rx) {
		err = -EBUSY;
		goto err_allocate_slot_2_rx;
	}

	struct hfc_pcm_slot *slot_2_tx = NULL;
	slot_2_tx = hfc_pcm_port_allocate_slot(&card->pcm_port, TX);
	if (!slot_2_tx) {
		err = -EBUSY;
		goto err_allocate_slot_2_tx;
	}

	chan->rx.fifo = fifo_1_rx;;
	chan->rx.fifo->connected_chan = &chan->rx;
	chan->tx.fifo = fifo_1_tx;
	chan->tx.fifo->connected_chan = &chan->tx;
	chan2->rx.fifo = fifo_2_rx;
	chan2->rx.fifo->connected_chan = &chan2->rx;
	chan2->tx.fifo = fifo_2_tx;
	chan2->tx.fifo->connected_chan = &chan2->tx;

	chan->rx.slot = slot_1_rx;
	chan->rx.slot->connected_chan = &chan->rx;
	chan->tx.slot = slot_1_tx;
	chan->tx.slot->connected_chan = &chan->tx;
	chan2->rx.slot = slot_2_rx;
	chan2->rx.slot->connected_chan = &chan2->rx;
	chan2->tx.slot = slot_2_tx;
	chan2->tx.slot->connected_chan = &chan2->tx;

	hfc_upload_fsm(card);

	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);

	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST_ST_from_PCM);

	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);

	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_ST_to_PCM);

	hfc_fifo_select(chan2->rx.fifo);
	hfc_fifo_reset(chan2->rx.fifo);

	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST_ST_from_PCM);

	hfc_fifo_select(chan2->tx.fifo);
	hfc_fifo_reset(chan2->tx.fifo);

	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_ST_to_PCM);

//	hfc_pcm_multireg_select(card, hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD1);
//	hfc_outb(card, hfc_R_PCM_MD1, hfc_R_PCM_MD1_V_PCM_LOOP);


	// Slot 0
	hfc_pcm_slot_select(card,
		(slot_1_rx->direction == RX ?
			hfc_R_SLOT_V_SL_DIR_RX :
			hfc_R_SLOT_V_SL_DIR_TX) |
		hfc_R_SLOT_V_SL_NUM(slot_1_rx->hw_index));

	hfc_outb(card, hfc_A_SL_CFG,
		hfc_A_SL_CFG_V_CH_SDIR_RX |
		hfc_A_SL_CFG_V_CH_NUM(chan->hw_index) |
		hfc_A_SL_CFG_V_ROUT_LOOP);

	hfc_pcm_slot_select(card,
		(slot_1_tx->direction == RX ?
			hfc_R_SLOT_V_SL_DIR_RX :
			hfc_R_SLOT_V_SL_DIR_TX) |
		hfc_R_SLOT_V_SL_NUM(slot_1_tx->hw_index));

	hfc_outb(card, hfc_A_SL_CFG,
		hfc_A_SL_CFG_V_CH_SDIR_TX |
		hfc_A_SL_CFG_V_CH_NUM(chan2->hw_index) |
		hfc_A_SL_CFG_V_ROUT_LOOP);

	// Slot 1
	hfc_pcm_slot_select(card,
		(slot_2_rx->direction == RX ?
			hfc_R_SLOT_V_SL_DIR_RX :
			hfc_R_SLOT_V_SL_DIR_TX) |
		hfc_R_SLOT_V_SL_NUM(slot_2_rx->hw_index));

	hfc_outb(card, hfc_A_SL_CFG,
		hfc_A_SL_CFG_V_CH_SDIR_RX |
		hfc_A_SL_CFG_V_CH_NUM(chan2->hw_index) |
		hfc_A_SL_CFG_V_ROUT_LOOP);

	hfc_pcm_slot_select(card,
		(slot_2_tx->direction == RX ?
			hfc_R_SLOT_V_SL_DIR_RX :
			hfc_R_SLOT_V_SL_DIR_TX) |
		hfc_R_SLOT_V_SL_NUM(slot_2_tx->hw_index));

	hfc_outb(card, hfc_A_SL_CFG,
		hfc_A_SL_CFG_V_CH_SDIR_TX |
		hfc_A_SL_CFG_V_CH_NUM(chan->hw_index) |
		hfc_A_SL_CFG_V_ROUT_LOOP);

	// Enable channel(s) in the port
	hfc_st_port_select(chan->port);
	hfc_st_port_update_st_ctrl_0(chan->port);
	hfc_st_port_update_st_ctrl_2(chan->port);

	hfc_st_port_select(chan2->port);
	hfc_st_port_update_st_ctrl_0(chan2->port);
	hfc_st_port_update_st_ctrl_2(chan2->port);

	hfc_card_unlock(card);
	
	return VISDN_CONNECT_BRIDGED;

	hfc_pcm_port_deallocate_slot(slot_1_tx);
err_allocate_slot_1_tx:
	hfc_pcm_port_deallocate_slot(slot_1_rx);
err_allocate_slot_1_rx:
	hfc_pcm_port_deallocate_slot(slot_2_tx);
err_allocate_slot_2_tx:
	hfc_pcm_port_deallocate_slot(slot_2_rx);
err_allocate_slot_2_rx:
	hfc_deallocate_fifo(fifo_2_tx);
err_allocate_fifo_2_tx:
	hfc_deallocate_fifo(fifo_2_rx);
err_allocate_fifo_2_rx:
	hfc_deallocate_fifo(fifo_1_tx);
err_allocate_fifo_1_tx:
	hfc_deallocate_fifo(fifo_1_rx);
err_allocate_fifo_1_rx:

	hfc_card_unlock(card);

	return err;
}

static int hfc_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan->connected_chan)
		return -EBUSY;

	struct hfc_chan_duplex *chan = visdn_chan->priv;
	struct hfc_chan_duplex *chan2 = visdn_chan2->priv;
	struct hfc_card *card = chan->port->card;

	hfc_debug_card(card, 2, "Connecting chan %s to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	if (chan->id != B1 && chan->id != B2) {
		hfc_msg(KERN_ERR, "Cannot connect %s to %s\n",
			chan->name, to_chan_duplex(visdn_chan2)->name);
		return -EINVAL;
	}

	if (visdn_chan->device.parent->parent ==
			visdn_chan2->device.parent->parent) {
		printk(KERN_DEBUG "Both channels belong to the me,"
			" attempting private bridge\n");

		return hfc_bridge(chan, chan2);
	}

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
	int roles)
{
	chan->port = port;
	chan->name = name;
	chan->status = HFC_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	chan->rx.chan = chan;
	chan->rx.direction = RX;

	chan->tx.chan = chan;
	chan->tx.direction = TX;

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


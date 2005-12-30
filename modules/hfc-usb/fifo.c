/*
 * Cologne Chip's HFC-USB vISDN driver
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
#include <linux/usb.h>

#include "hfc-usb.h"
#include "fifo.h"
#include "card.h"
#include "card_inline.h"
#include "st_chan.h"
#include "st_port.h"

#ifdef DEBUG_CODE
#define hfc_debug_fifo(fifo, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st:"						\
			"chan[%s] "					\
			format,						\
			(fifo)->chan->port->card->usb_dev->dev.bus->name,\
			(fifo)->chan->port->card->usb_dev->dev.bus_id,	\
			(fifo)->chan->visdn_chan.name,			\
			## arg)
#else
#define hfc_debug_fifo(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_fifo(fifo, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st:"							\
		"chan[%s] "						\
		format,							\
		(fifo)->chan->port->card->usb_dev->dev.bus->name,	\
		(fifo)->chan->port->card->usb_dev->dev.bus_id,		\
		(fifo)->chan->visdn_chan.name,				\
		## arg)

int hfc_fifo_is_running(struct hfc_fifo *fifo)
{
	if (!fifo->enabled ||
	    (((fifo->chan->port->nt_mode &&
	      fifo->chan->port->l1_state != 3) ||
	     (!fifo->chan->port->nt_mode &&
	      fifo->chan->port->l1_state != 7)))) {
		return FALSE;
	} else {
		return TRUE;
	}
}

void hfc_fifo_configure(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->chan->port->card;
	u8 con_hdlc = 0;
	u8 subch;

	if (!hfc_fifo_is_running(fifo)) {
		hfc_write(card, HFC_REG_CON_HDLC,
			HFC_REG_CON_HDLC_HDLC |
			HFC_REG_CON_HDLC_IFF |
			HFC_REG_CON_FIFO_from_ST |
			HFC_REG_CON_ST_from_FIFO |
			HFC_REG_CON_PCM_from_FIFO);

		return;
	}

	if (fifo->framer_enabled) {
		con_hdlc |= HFC_REG_CON_HDLC_HDLC |
				HFC_REG_CON_HDLC_IFF |
				HFC_REG_CON_HDLC_HDLC_ENA;
	} else {
		con_hdlc |= HFC_REG_CON_HDLC_TRANS |
				HFC_REG_CON_HDLC_TRANS_64;
	}

	con_hdlc |= HFC_REG_CON_FIFO_from_ST |
			HFC_REG_CON_ST_from_FIFO |
			HFC_REG_CON_PCM_from_FIFO;

	hfc_write(card, HFC_REG_CON_HDLC, con_hdlc);

	switch (fifo->subchannel_bit_count) {
		case 1: subch = HFC_REG_HDLC_PAR_PROC_1BITS; break;
		case 2: subch = HFC_REG_HDLC_PAR_PROC_2BITS; break;
		case 3: subch = HFC_REG_HDLC_PAR_PROC_3BITS; break;
		case 4: subch = HFC_REG_HDLC_PAR_PROC_4BITS; break;
		case 5: subch = HFC_REG_HDLC_PAR_PROC_5BITS; break;
		case 6: subch = HFC_REG_HDLC_PAR_PROC_6BITS; break;
		case 7: subch = HFC_REG_HDLC_PAR_PROC_7BITS; break;
		case 8: subch = HFC_REG_HDLC_PAR_PROC_8BITS; break;
		default:
			WARN_ON(1);
			subch = HFC_REG_HDLC_PAR_PROC_8BITS;
	}

	subch |= HFC_REG_HDLC_PAR_START_0BIT;

	hfc_write(card, HFC_REG_HDLC_PAR, subch);
}

void hfc_fifo_reset(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->chan->port->card;

	hfc_write(card, HFC_REG_INC_RES_F,
		HFC_REG_INC_RES_F_RESET);
}

void hfc_fifo_init(
	struct hfc_fifo *fifo, 
	struct hfc_st_chan *chan,
	int hw_index,
	enum hfc_direction direction,
	int subchannel_bit_count)
{
	memset(fifo, 0, sizeof(*fifo));

	fifo->chan = chan;
	fifo->hw_index = hw_index;
	fifo->direction = direction;

	fifo->subchannel_bit_start = 0;
	fifo->subchannel_bit_count = subchannel_bit_count;
}

struct hfc_fifo_control
{
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 state:4;
	u8 :2;
	u8 err:1;
	u8 eof:1;

	u8 fill_pcm_rx:1;
	u8 fill_pcm_tx:1;
	u8 fill_d_rx:1;
	u8 fill_d_tx:1;
	u8 fill_b2_rx:1;
	u8 fill_b2_tx:1;
	u8 fill_b1_rx:1;
	u8 fill_b1_tx:1;

#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 eof:1;
	u8 err:1;
	u8 :2;
	u8 state:4;

	u8 fill_b1_tx:1;
	u8 fill_b1_rx:1;
	u8 fill_b2_tx:1;
	u8 fill_b2_rx:1;
	u8 fill_d_tx:1;
	u8 fill_d_rx:1;
	u8 fill_pcm_tx:1;
	u8 fill_pcm_rx:1;
#endif
} __attribute__ ((__packed__));

static void hfc_fifo_is_now_ready(struct hfc_fifo *fifo)
{
	if (test_and_clear_bit(HFC_FIFO_FLAG_WAITING_BUSY, &fifo->flags))
		visdn_leg_wake_queue(&fifo->chan->visdn_chan.leg_b);
}

static void hfc_fifo_rx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct hfc_fifo *fifo = urb->context;
	struct hfc_st_chan *chan = fifo->chan;
	struct hfc_stat { u8 crc[2], stat; } __attribute((packed)) *stat;
	struct hfc_fifo_control *fifo_control =
		(struct hfc_fifo_control *)fifo->urb_buf;
	int err;

	if (fifo_control->state != chan->port->l1_state)
		schedule_work(&chan->port->state_change_work);

	if (!fifo_control->fill_d_tx)
		hfc_fifo_is_now_ready(&fifo->chan->port->chans[D].tx_fifo);

	if (!fifo_control->fill_b1_tx)
		hfc_fifo_is_now_ready(&fifo->chan->port->chans[B1].tx_fifo);

	if (!fifo_control->fill_b2_tx)
		hfc_fifo_is_now_ready(&fifo->chan->port->chans[B2].tx_fifo);

	if (!fifo_control->fill_pcm_tx)
		hfc_fifo_is_now_ready(&fifo->chan->port->chans[E].tx_fifo);

	if (fifo->frame_buf_len + urb->actual_length - sizeof(*fifo_control) >
			sizeof(fifo->frame_buf)) {

		// TODO
		goto err_frame_overflow;
	}

	memcpy(fifo->frame_buf + fifo->frame_buf_len,
		fifo->urb_buf + sizeof(*fifo_control),
		urb->actual_length - sizeof(*fifo_control));

	fifo->frame_buf_len += urb->actual_length - sizeof(*fifo_control);

	if (fifo_control->eof) {
		struct sk_buff *skb;
		int frame_buf_len = fifo->frame_buf_len;

		fifo->frame_buf_len = 0;

		if (frame_buf_len < 3) {
			hfc_debug_fifo(fifo, 2,
				"invalid frame received,"
				" just %d bytes\n",
				frame_buf_len);

			visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_DROPPED);

			goto out;
		} else if(frame_buf_len == 3) {
			hfc_debug_fifo(fifo, 2,
				"empty frame received\n");

			visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_DROPPED);

			goto out;
		}

#ifdef DEBUG_CODE
		if (debug_level >= 3) {
			int i;

			hfc_debug_fifo(fifo, 3, "RX len %2d: ", frame_buf_len);

			for (i=0; i<frame_buf_len; i++)
				printk("%02x", ((u8 *)fifo->frame_buf)[i]);

			printk("\n");
		}
#endif

		stat = (struct hfc_stat *)(fifo->frame_buf + frame_buf_len - 3);

		if (stat->stat == 0xff) {
			// Frame abort detected

			hfc_debug_fifo(fifo, 3, "Frame abort detected\n");

			visdn_leg_rx_error(&chan->visdn_chan.leg_b,
					VISDN_RX_ERROR_FR_ABORT);

			goto err_frame_abort;

		} else if (stat->stat != 0x00) {
			// CRC not ok, frame broken, skipping
			hfc_debug_fifo(fifo, 2,
				"Received frame with wrong CRC\n");

			visdn_leg_rx_error(&chan->visdn_chan.leg_b,
					VISDN_RX_ERROR_CRC);

			goto err_crc_error;
		}

		skb = dev_alloc_skb(frame_buf_len - 3);
		if (!skb) {
			hfc_msg_fifo(fifo, KERN_ERR,
				"cannot allocate skb: frame dropped\n");

			visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_DROPPED);

			goto err_alloc_skb;
		}

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		memcpy(skb_put(skb, frame_buf_len - 3),
			fifo->frame_buf,
			frame_buf_len - 3);

		visdn_leg_frame_xmit(&chan->visdn_chan.leg_b, skb);

		{
		struct hfc_led *led = fifo->chan->led;

		led->flashing_freq = HZ / 10;
		led->flashes = 1;
		hfc_led_update(led);
		}
	}

err_alloc_skb:
err_crc_error:
err_frame_abort:
err_frame_overflow:
out:

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"can't resubmit urb, error %d\n", err);
	}
}

static void hfc_fifo_tx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct hfc_fifo *fifo = urb->context;

	set_bit(HFC_FIFO_FLAG_WAITING_BUSY,
		&fifo->chan->tx_fifo.flags);
}

int hfc_fifo_xmit(struct hfc_fifo *fifo, void *buf, int len)
{
	int err;

#ifdef DEBUG_CODE
	if (debug_level >= 3) {
		int i;

		hfc_debug_fifo(fifo, 3, "TX len %2d: ", len);

		for (i=0; i<len; i++)
			printk("%02x",((u8 *)buf)[i]);

		printk("\n");
	}
#endif

	if (len > sizeof(fifo->urb_buf)) {
		err = VISDN_TX_BUSY;
		goto err_frame_too_big;
	}

	fifo->urb_buf[0] = 0x01;
	fifo->urb->transfer_buffer_length = len + 1;
	memcpy(fifo->urb_buf + 1, buf, len);

	{
	struct hfc_led *led = fifo->chan->led;

	led->flashing_freq = HZ / 10;
	led->flashes = 1;
	hfc_led_update(led);
	}

	/* Do not allow netdev layer to send other frames until our URBs have
	 * been ACKed and the FIFO has space
	 */

	visdn_leg_stop_queue(&fifo->chan->visdn_chan.leg_b);

	err = usb_submit_urb(fifo->urb, GFP_ATOMIC);
	if (err) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"can't submit urb, error %d\n", err);
		goto err_submit_urb;
	}

	return VISDN_TX_OK;

err_submit_urb:
err_frame_too_big:

	return VISDN_TX_BUSY;
}

int hfc_fifo_alloc(struct hfc_fifo *fifo)
{
	struct hfc_st_chan *chan = fifo->chan;
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;
	int err;

	fifo->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!fifo->urb) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"usb_alloc_urb() error\n");
		err = -ENOMEM;
		goto err_alloc_urb;
	}

	if (fifo->direction == TX) {
		fifo->pipe = usb_sndintpipe(
			card->usb_dev,
			fifo->int_endpoint->desc.bEndpointAddress);

		usb_fill_int_urb(
			fifo->urb,
			card->usb_dev,
			fifo->pipe,
			fifo->urb_buf,
			0,
			hfc_fifo_tx_complete,
			fifo, 1);

	} else {
		fifo->pipe = usb_rcvintpipe(
			card->usb_dev,
			fifo->int_endpoint->desc.bEndpointAddress);

		usb_fill_int_urb(
			fifo->urb,
			card->usb_dev,
			fifo->pipe,
			fifo->urb_buf,
			sizeof(fifo->urb_buf),
			hfc_fifo_rx_complete,
			fifo, 1);
	}

	return 0;

//	usb_free_urb()
err_alloc_urb:

	return err;
}

void hfc_fifo_dealloc(struct hfc_fifo *fifo)
{
//	usb_free_urb
}

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
#include <linux/pci.h>
#include <linux/netdevice.h>

#include "fifo.h"
#include "fifo_inline.h"
#include "card.h"
#include "card_inline.h"

int hfc_fifo_mem_read(struct hfc_fifo *fifo,
	void *data, int size)
{
	struct hfc_card *card = fifo->card;

	int i;
	for (i=0; i<size; i++) {
		((u8 *)data)[i] = hfc_inb(card, hfc_A_FIFO_DATA0);
	}

	return size;
}

int hfc_fifo_mem_read_to_user(struct hfc_fifo *fifo,
	void __user *data, int size)
{
	struct hfc_card *card = fifo->card;
	int err;

	int i;
	for (i=0; i<size; i++) {
		err = put_user(hfc_inb(card, hfc_A_FIFO_DATA0),
				(u8 *)(data + i));

		if (err < 0)
			return err;
	}

	return size;
}

void hfc_fifo_mem_write(struct hfc_fifo *fifo,
	const void *data, int size)
{
	struct hfc_card *card = fifo->card;

	int i;
	for (i=0; i<size; i++) {
		hfc_outb(card,
			hfc_A_FIFO_DATA0,
			((u8 *)data)[i]);
	}

/*
	int offset = 0;

	// Ensure alignment with u16
	if ((uintptr_t)(data + offset) % sizeof(u16)) {
		hfc_outb(chan->chan->port->card,
			hfc_A_FIFO_DATA0,
			*((u8 *)(data + offset)));

		offset += sizeof(u8);
	}

	// Ensure alignment with u32
	if ((uintptr_t)(data + offset) % sizeof(u32)) {
		hfc_outb(chan->chan->port->card,
			hfc_A_FIFO_DATA1,
			*((u16 *)(data + offset)));

		offset += sizeof(u16);
	}

	// Write u32 while there is space
	while (size - offset >= sizeof(u32)) {
		hfc_outb(chan->chan->port->card,
			hfc_A_FIFO_DATA2,
			*((u32 *)(data + offset)));

		offset += sizeof(u32);
	}

	// Write remaining u16
	while (size - offset >= sizeof(u16)) {
		hfc_outb(chan->chan->port->card,
			hfc_A_FIFO_DATA1,
			*((u16 *)(data + offset)));

		offset += sizeof(u16);
	}

	// Write remaining u8
	while (size - offset >= sizeof(u8)) {
		hfc_outb(chan->chan->port->card,
			hfc_A_FIFO_DATA0,
			*((u8 *)(data + offset)));

		offset += sizeof(u8);
	}*/
}

void hfc_fifo_mem_write_from_user(
	struct hfc_fifo *fifo,
	const void __user *data, int size)
{
	struct hfc_card *card = fifo->card;

	int i;
	for (i=0; i<size; i++) {
		u8 val;
		get_user(val, (u8 *)(data + i));

		hfc_outb(card,
			hfc_A_FIFO_DATA0,
			val);
	}
}

void hfc_fifo_drop(struct hfc_fifo *fifo, int size)
{
	int available_bytes = hfc_fifo_used_rx(fifo);
	if (available_bytes + 1 < size) {
		hfc_msg_fifo(fifo, KERN_WARNING,
			"RX FIFO not enough (%d) bytes to drop!\n",
			available_bytes);

		return;
	}

	// FIXME read and drop bytes
}

void hfc_fifo_drop_frame(struct hfc_fifo *fifo)
{
	// FIXME read and drop all the frame

	hfc_fifo_next_frame(fifo);
}

void hfc_fifo_rx_work(void *data)
{
	struct hfc_fifo *fifo = data;

	WARN_ON(!fifo->connected_chan);
	if (!fifo->connected_chan)
		return;

	struct hfc_chan_simplex *chan = fifo->connected_chan;
	struct hfc_chan_duplex *fdchan = chan->chan;
	struct hfc_card *card = fdchan->port->card;

	down(&card->sem);

	// FIFO selection has to be done for each frame to clear
	// internal buffer (see specs 4.4.4).
	hfc_fifo_select(fifo);

	hfc_fifo_refresh_fz_cache(fifo);

	if (!hfc_fifo_has_frames(fifo))
		goto no_frames;

	// frame_size includes CRC+CRC+STAT
	int frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size < 3) {
		hfc_debug_fifo(fifo, 3,
			"invalid frame received, just %d bytes\n",
			frame_size);

		fdchan->stats.rx_errors++;
		fdchan->stats.rx_length_errors++;

		goto err_invalid_frame;
	} else if(frame_size == 3) {
		hfc_debug_fifo(fifo, 3,
			"empty frame received\n");

		fdchan->stats.rx_errors++;
		fdchan->stats.rx_length_errors++;

		goto err_empty_frame;
	}

	struct sk_buff *skb =
		visdn_alloc_skb(frame_size - 3);

	if (!skb) {
		hfc_msg_fifo(fifo, KERN_ERR,
			"cannot allocate skb: frame dropped\n");

		fdchan->stats.rx_dropped++;

		goto err_alloc_skb;
	}

	hfc_fifo_mem_read(fifo, skb_put(skb, frame_size - 3), frame_size - 3);

	struct { u8 crc[2], stat; } __attribute((packed)) stat;

	hfc_fifo_mem_read(fifo, &stat, sizeof(stat));

#ifdef DEBUG
	if(debug_level == 3) {
		hfc_debug_fifo(fifo, 3, "RX len %2d: ", frame_size);
	} else if(debug_level >= 4) {
		hfc_debug_fifo(fifo, 4,
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			fifo->f1, fifo->f2, fifo->z1, fifo->z2,
			frame_size);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<frame_size - 3; i++)
			printk("%02x", ((u8 *)skb->data)[i]);

		printk("%02x%02x %02x\n", stat.crc[0], stat.crc[1], stat.stat);
	}
#endif

	if (stat.stat == 0xff) {
		// Frame abort detected

		hfc_debug_fifo(fifo, 3, "Frame abort detected\n");

		fdchan->stats.rx_errors++;
		fdchan->stats.collisions++;

		goto err_frame_abort;

	} else if (stat.stat != 0x00) {
		// CRC not ok, frame broken, skipping
		hfc_debug_fifo(fifo, 2, "Received frame with wrong CRC\n");

		fdchan->stats.rx_errors++;
		fdchan->stats.rx_crc_errors++;

		goto err_crc_error;
	}

	hfc_fifo_next_frame(fifo);

	fdchan->stats.rx_packets++;
	// STAT is not really received on wire
	fdchan->stats.rx_bytes += frame_size - 1;

	visdn_frame_rx(&fdchan->visdn_chan, skb);

	goto all_went_well;

err_crc_error:
err_frame_abort:
	kfree_skb(skb);
err_alloc_skb:
err_empty_frame:
err_invalid_frame:
	hfc_fifo_drop_frame(fifo);
no_frames:
all_went_well:

	if (hfc_fifo_has_frames(fifo))
		schedule_work(&fifo->work);

	up(&card->sem);
}

void hfc_fifo_configure(
	struct hfc_fifo *fifo)
{
	WARN_ON(atomic_read(&fifo->card->sem.count) > 0);

	struct hfc_card *card = fifo->card;

	u8 subch_bits;
	switch (fifo->bitrate) {
		case  8000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_1; break;
		case 16000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_2; break;
		case 24000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_3; break;
		case 32000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_4; break;
		case 40000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_5; break;
		case 48000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_6; break;
		case 56000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_7; break;
		case 64000: subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_8; break;
		default:
			WARN_ON(1);
			subch_bits = hfc_A_SUBCH_CFG_V_BIT_CNT_8;
	}

	hfc_outb(card, hfc_A_SUBCH_CFG, subch_bits);

	u8 con_hdlc = 0;

	if (fifo->framing == VISDN_CHAN_FRAMING_TRANS) {
		con_hdlc |= hfc_A_CON_HDCL_V_HDLC_TRP_TRP |
			    hfc_A_CON_HDCL_V_TRP_IRQ_4096;
	} else if (fifo->framing == VISDN_CHAN_FRAMING_HDLC) {
		con_hdlc |= hfc_A_CON_HDCL_V_HDLC_TRP_HDLC |
			    hfc_A_CON_HDCL_V_IFF |
			    hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED;
	}

	if (fifo->direction == RX) {
		if (fifo->connect_to == HFC_FIFO_CONNECT_TO_ST)
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST;
		else
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST_ST_from_PCM;
	} else {
		if (fifo->connect_to == HFC_FIFO_CONNECT_TO_ST)
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM;
		else
			con_hdlc |= hfc_A_CON_HDCL_V_DATA_FLOW_ST_to_PCM;
	}

	hfc_outb(card, hfc_A_CON_HDLC, con_hdlc);
}

void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction)
{
	fifo->card = card;
	fifo->hw_index = hw_index;
	fifo->direction = direction;

	if (fifo->direction == RX)
		INIT_WORK(&fifo->work,
			hfc_fifo_rx_work,
			fifo);
}

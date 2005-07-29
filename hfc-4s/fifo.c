/*
 * fifo.c
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/cdev.h>

#include "hfc-4s.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "card.h"
#include "card_inline.h"

static inline void hfc_fifo_next_frame(struct hfc_fifo *fifo)
{
	struct hfc_card *card = fifo->card;

	hfc_outb(card, hfc_A_INC_RES_FIFO,
		hfc_A_INC_RES_FIFO_V_INC_F);

	hfc_wait_busy(card);
}

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

static void hfc_fifo_mem_write(struct hfc_fifo *fifo,
	void *data, int size)
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

int hfc_fifo_get(struct hfc_fifo *fifo,
		void *data, int size)
{
	struct hfc_card *card = fifo->card;

	// Some useless statistic
	// fifo->chan->bytes += size;

	int available_bytes = hfc_fifo_used_rx(fifo);

	if (available_bytes < size) {
		hfc_msg_fifo(fifo, KERN_WARNING,
			"RX FIFO not enough (%d) bytes to receive!\n",
			available_bytes);

		return -1;
	}

	hfc_fifo_mem_read(fifo, data, size);

	hfc_outb(card, hfc_A_INC_RES_FIFO, hfc_A_INC_RES_FIFO_V_INC_F);
	hfc_wait_busy(card);

	return available_bytes - size;
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

void hfc_fifo_put(struct hfc_fifo *fifo,
			void *data, int size)
{
	hfc_fifo_mem_write(fifo, data, size);

	// fifo->chan->bytes += size;
}

int hfc_fifo_get_frame(struct hfc_fifo *fifo, void *data, int max_size)
{

	if (fifo->f1 == fifo->f2) {
		// nothing received, strange uh?
		hfc_msg_fifo(fifo, KERN_WARNING,
			"get_frame called with no frame in FIFO.\n");

		return -1;
	}

	// frame_size includes CRC+CRC+STAT
	int frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size <= 0) {
		hfc_debug_fifo(fifo, 2, "invalid (empty) frame received.\n");

		hfc_fifo_drop_frame(fifo);
		return -1;
	}

	// STAT is not really received on wire
	// fifo->chan->bytes += frame_size - 1;

#ifdef DEBUG
	if(debug_level == 3) {
		hfc_debug_fifo(fifo, 3, "RX len %2d: ", frame_size);
	} else if(debug_level >= 4) {
		hfc_debug_fifo(fifo, 4,
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			fifo->f1, fifo->f2, fifo->z1, fifo->z2,
			frame_size);
	}
#endif

	int read_bytes =
		hfc_fifo_mem_read(fifo, data,
			frame_size < max_size ? frame_size : max_size);

	int unread_bytes = frame_size - read_bytes;

#ifdef DEBUG
	if (debug_level >= 3) {
		int i;
		for (i=0; i<read_bytes; i++)
			printk("%02x", ((u8 *)data)[i]);
	}

	if (unread_bytes > 1 && debug_level >= 3)
		printk(" ");
#endif

	while (unread_bytes > 1) {
		u8 trash;
		hfc_fifo_mem_read(fifo, &trash, 1);
		unread_bytes--;

#ifdef DEBUG
		if (debug_level >= 3)
			printk("%02x", trash);
#endif
	}

	u8 stat;
	hfc_fifo_mem_read(fifo, &stat, sizeof(stat));

#ifdef DEBUG
	if (debug_level >= 3)
		printk(" %02x\n", stat); 
#endif

	if (stat == 0xff) {
		// Frame abort detected

		hfc_debug_fifo(fifo, 3, "Frame abort detected\n");

		hfc_fifo_drop_frame(fifo);
		return -1;
	} else if (stat != 0x00) {
		// CRC not ok, frame broken, skipping
		hfc_debug_fifo(fifo, 2, "Received frame with wrong CRC\n");

		// fifo->chan->crc++;
		// fifo->chan->chan->net_device_stats.rx_errors++;

		hfc_fifo_drop_frame(fifo);
		return -1;
	}

	// fifo->chan->frames++;

	hfc_fifo_next_frame(fifo);

	return frame_size;
}

void hfc_fifo_drop_frame(struct hfc_fifo *fifo)
{
	// FIXME read and drop all the frame

	hfc_fifo_next_frame(fifo);
}

void hfc_fifo_put_frame(struct hfc_fifo *fifo,
		 void *data, int size)
{
#ifdef DEBUG
	if (debug_level == 3) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_fifo(fifo, 3, "TX len %2d: ", size);

	} else if (debug_level >= 4) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_fifo(fifo, 4,
			"TX (f1=%02x, f2=%02x, z1=N/A, z2=%04x) len %2d: ",
			fifo->f1, fifo->f2, fifo->z2,
			size);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<size; i++)
			printk("%02x",((u8 *)data)[i]);

		printk("\n");
	}
#endif

	hfc_fifo_put(fifo, data, size);

	hfc_fifo_next_frame(fifo);

	// fifo->chan->frames++;
}

void hfc_fifo_rx_tasklet(unsigned long data)
{
	struct hfc_fifo *fifo = (struct hfc_fifo *)data;

	if (!fifo->connected_chan) {
		hfc_msg(KERN_INFO, "Spurious interrupt\n");
		return;
	}

	struct hfc_chan_simplex *chan = fifo->connected_chan;
	struct hfc_chan_duplex *fdchan = chan->chan;
	struct hfc_card *card = fdchan->port->card;


	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	// FIFO selection has to be done for each frame to clear
	// internal buffer (see specs 4.4.4).
	hfc_fifo_select(fifo);

	hfc_fifo_refresh_fz_cache(fifo);

	if (!hfc_fifo_has_frames(fifo))
		goto no_frames;

	int frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size < 3) {
		hfc_debug_fifo(fifo, 2,
			"invalid frame received, just %d bytes\n",
			frame_size);

		hfc_fifo_drop_frame(fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_invalid_frame;
	} else if(frame_size == 3) {
		hfc_debug_fifo(fifo, 2,
			"empty frame received\n");

		hfc_fifo_drop_frame(fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_empty_frame;
	}

	struct sk_buff *skb =
		visdn_alloc_skb(frame_size - 3);

	if (!skb) {
		hfc_msg_fifo(fifo, KERN_ERR,
			"cannot allocate skb: frame dropped\n");

		hfc_fifo_drop_frame(fifo);

		fdchan->net_device_stats.rx_dropped++;

		goto err_alloc_skb;
	}

	if (hfc_fifo_get_frame(fifo,
		skb_put(skb, frame_size - 3),
		frame_size - 3) == -1) {
		goto err_get_frame;
	}

	fdchan->net_device_stats.rx_packets++;
	fdchan->net_device_stats.rx_bytes += frame_size - 1;

	visdn_frame_rx(&fdchan->visdn_chan, skb);

	goto all_went_well;

err_get_frame:
	kfree_skb(skb);
err_alloc_skb:
err_empty_frame:
err_invalid_frame:
no_frames:
all_went_well:

	if (hfc_fifo_has_frames(fifo))
		tasklet_schedule(&fifo->tasklet);

	spin_unlock_irqrestore(&card->lock,flags);
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
		tasklet_init(&fifo->tasklet,
			hfc_fifo_rx_tasklet,
			(unsigned long)fifo);
}

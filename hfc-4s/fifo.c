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

#include "hfc-4s.h"
#include "fifo.h"

static inline void hfc_fifo_next_frame(struct hfc_chan_simplex *chan)
{
	hfc_outb(chan->chan->port->card, hfc_A_INC_RES_FIFO,
		hfc_A_INC_RES_FIFO_V_INC_F);
	hfc_wait_busy(chan->chan->port->card);
}

static int hfc_fifo_mem_read(struct hfc_chan_simplex *chan,
	void *data, int size)
{
	int i;
	for (i=0; i<size; i++) {
		((u8 *)data)[i] = hfc_inb(chan->chan->port->card,
			hfc_A_FIFO_DATA0);
#ifdef DEBUG
		if(debug_level >= 3)
			printk("%02x", ((u8 *)data)[i]);
#endif
	}

	return size;
}

static void hfc_fifo_mem_write(struct hfc_chan_simplex *chan,
	void *data, int size)
{
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
	}
}

int hfc_fifo_get(struct hfc_chan_simplex *chan,
		void *data, int size)
{
	// Some useless statistic
	chan->bytes += size;

	int available_bytes = hfc_fifo_used_rx(chan);

	if (available_bytes < size) {
		printk(KERN_WARNING hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"RX FIFO not enough (%d) bytes to receive!\n",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			available_bytes);

		return -1;
	}

	hfc_fifo_mem_read(chan, data, size);

	hfc_outb(chan->chan->port->card, hfc_A_INC_RES_FIFO,
		hfc_A_INC_RES_FIFO_V_INC_F);
	hfc_wait_busy(chan->chan->port->card);

	return available_bytes - size;
}

void hfc_fifo_drop(struct hfc_chan_simplex *chan, int size)
{
	int available_bytes = hfc_fifo_used_rx(chan);
	if (available_bytes + 1 < size) {
		printk(KERN_WARNING hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"RX FIFO not enough (%d) bytes to drop!\n",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			available_bytes);

		return;
	}

	// FIXME read and drop bytes
}

void hfc_fifo_put(struct hfc_chan_simplex *chan,
			void *data, int size)
{
	int free_bytes = hfc_fifo_free_tx(chan);
	if (free_bytes < size) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"TX FIFO full!\n",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name);

		chan->fifo_full++;

		hfc_fifo_reset(chan->chan->port->card);
	}

	hfc_fifo_mem_write(chan, data, size);

	chan->bytes += size;
}

int hfc_fifo_get_frame(struct hfc_chan_simplex *chan, void *data, int max_size)
{

	if (chan->f1 == chan->f2) {
		// nothing received, strange uh?
		printk(KERN_WARNING hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"get_frame called with no frame in FIFO.\n",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name);

		return -1;
	}

	// frame_size includes CRC+CRC+STAT
	int frame_size = hfc_fifo_get_frame_size(chan);

	if (frame_size <= 0) {
#ifdef DEBUG
		if (debug_level >= 2) {
			printk(KERN_DEBUG hfc_DRIVER_PREFIX
				"card %d: "
				"port %d: "
				"chan %s: "
				"invalid (empty) frame received.\n",
				chan->chan->port->card->id,
				chan->chan->port->id,
				chan->chan->name);
		}
#endif

		hfc_fifo_drop_frame(chan);
		return -1;
	}

	// STAT is not really received on wire
	chan->bytes += frame_size - 1;

#ifdef DEBUG
	if(debug_level == 3) {
		printk(KERN_DEBUG hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"RX len %2d: ",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			frame_size);
	} else if(debug_level >= 4) {
		printk(KERN_DEBUG hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			chan->f1, chan->f2, chan->z1, chan->z2,
			frame_size);
	}
#endif

	int unread_bytes = frame_size -
		hfc_fifo_mem_read(chan, data,
			frame_size < max_size ? frame_size : max_size);

	while (unread_bytes > 1) {
		u8 trash;
		hfc_fifo_mem_read(chan, &trash, 1);
		unread_bytes--;
	}

	u8 stat;
	hfc_fifo_mem_read(chan, &stat, sizeof(stat));

#ifdef DEBUG
	if (debug_level >= 3)
		printk("\n"); 
#endif

	if (stat == 0xff) {
		// Frame abort detected
#ifdef DEBUG
		if(debug_level >= 2) {
			printk(KERN_WARNING hfc_DRIVER_PREFIX
				"card %d: "
				"port %d: "
				"chan %s: "
				"Frame abort detected\n",
				chan->chan->port->card->id,
				chan->chan->port->id,
				chan->chan->name);
		}
#endif

		hfc_fifo_drop_frame(chan);
		return -1;
	} else if (stat != 0x00) {
		// CRC not ok, frame broken, skipping
#ifdef DEBUG
		if(debug_level >= 2) {
			printk(KERN_WARNING hfc_DRIVER_PREFIX
				"card %d: "
				"port %d: "
				"chan %s: "
				"Received frame with wrong CRC\n",
				chan->chan->port->card->id,
				chan->chan->port->id,
				chan->chan->name);
		}
#endif

		chan->crc++;
		chan->chan->net_device_stats.rx_errors++;

		hfc_fifo_drop_frame(chan);
		return -1;
	}

	chan->frames++;

	hfc_fifo_next_frame(chan);

	return frame_size;
}

void hfc_fifo_drop_frame(struct hfc_chan_simplex *chan)
{
	// FIXME read and drop all the frame

	hfc_fifo_next_frame(chan);
}

void hfc_fifo_put_frame(struct hfc_chan_simplex *chan,
		 void *data, int size)
{
#ifdef DEBUG
	if (debug_level == 3) {
		hfc_fifo_refresh_fz_cache(chan);

		printk(KERN_DEBUG hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"TX len %2d: ",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			size);
	} else if (debug_level >= 4) {
		printk(KERN_DEBUG hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"TX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name,
			chan->f1, chan->f2, chan->z1, chan->z2,
			size);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<size; i++)
			printk("%02x",((u8 *)data)[i]);

		printk("\n");
	}
#endif

	int available_frames = hfc_fifo_free_frames(chan);

	if (available_frames >= chan->f_num) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"TX FIFO total number of frames exceeded!\n",
			chan->chan->port->card->id,
			chan->chan->port->id,
			chan->chan->name);

		chan->fifo_full++;

		hfc_fifo_reset(chan->chan->port->card);

		return;
	}

	hfc_fifo_put(chan, data, size);

	hfc_fifo_next_frame(chan);

	chan->frames++;
}

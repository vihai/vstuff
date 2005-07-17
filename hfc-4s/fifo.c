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
		hfc_msg_fifo(KERN_WARNING, fifo,
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
		hfc_msg_fifo(KERN_WARNING, fifo,
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
		hfc_msg_fifo(KERN_WARNING, fifo,
			"get_frame called with no frame in FIFO.\n");

		return -1;
	}

	// frame_size includes CRC+CRC+STAT
	int frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size <= 0) {
		hfc_debug_fifo(2, fifo, "invalid (empty) frame received.\n");

		hfc_fifo_drop_frame(fifo);
		return -1;
	}

	// STAT is not really received on wire
	// fifo->chan->bytes += frame_size - 1;

#ifdef DEBUG
	if(debug_level == 3) {
		hfc_debug_fifo(3, fifo, "RX len %2d: ", frame_size);
	} else if(debug_level >= 4) {
		hfc_debug_fifo(4, fifo,
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			fifo->f1, fifo->f2, fifo->z1, fifo->z2,
			frame_size);
	}
#endif

	int unread_bytes = frame_size -
		hfc_fifo_mem_read(fifo, data,
			frame_size < max_size ? frame_size : max_size);

	while (unread_bytes > 1) {
		u8 trash;
		hfc_fifo_mem_read(fifo, &trash, 1);
		unread_bytes--;
	}

	u8 stat;
	hfc_fifo_mem_read(fifo, &stat, sizeof(stat));

#ifdef DEBUG
	if (debug_level >= 3)
		printk("\n"); 
#endif

	if (stat == 0xff) {
		// Frame abort detected

		hfc_debug_fifo(3, fifo, "Frame abort detected\n");

		hfc_fifo_drop_frame(fifo);
		return -1;
	} else if (stat != 0x00) {
		// CRC not ok, frame broken, skipping
		hfc_debug_fifo(2, fifo, "Received frame with wrong CRC\n");

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
		hfc_debug_fifo(3, fifo, "TX len %2d: ", size);

	} else if (debug_level >= 4) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_fifo(4, fifo,
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

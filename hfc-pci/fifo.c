/*
 * hfc.c - Salcazzo driver for HFC-S PCI A based ISDN BRI cards
 *
 * Copyright (C) 2004 Daniele Orlandi
 * Copyright (C) 2002, 2003, 2004, Junghanns.NET GmbH
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * Major rewrite of the driver made by
 * Klaus-Peter Junghanns <kpj@junghanns.net>
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 * Please read the README file for important infos.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/netdevice.h>

#include "card.h"
#include "fifo.h"
#include "fifo_inline.h"

void hfc_fifo_clear_rx(struct hfc_fifo *fifo)
{
	*fifo->f2 = *fifo->f1;
	*Z2_F2(fifo) = *Z1_F2(fifo);
}

static void hfc_fifo_mem_read(struct hfc_fifo *fifo,
	int z_start,
	void *data,
	int size)
{
	int octets_to_boundary = fifo->z_max - z_start + 1;
	if (octets_to_boundary >= size) {
		memcpy(data,
			fifo->z_base + z_start,
			size);
	} else {
		// Buffer wrap
		memcpy(data,
			fifo->z_base + z_start,
			octets_to_boundary);

		memcpy(data + octets_to_boundary,
			fifo->fifo_base,
			size - octets_to_boundary);
	}
}

void hfc_fifo_mem_write(struct hfc_fifo *fifo,
	void *data, int size)
{
	int octets_to_boundary = fifo->z_max - *Z1_F1(fifo) + 1;
	if (octets_to_boundary >= size) {
		memcpy(fifo->z_base + *Z1_F1(fifo),
			data,
			size);
	} else {
		// FIFO wrap

		memcpy(fifo->z_base + *Z1_F1(fifo),
			data,
			octets_to_boundary);

		memcpy(fifo->fifo_base,
			data + octets_to_boundary,
			size - octets_to_boundary);
	}
}

int hfc_fifo_mem_read_user(
	struct hfc_fifo *fifo,
	void __user *buf,
	int size)
{
	int octets_to_boundary = fifo->z_max - *Z2_F2(fifo) + 1;
	if (octets_to_boundary >= size) {
		if (copy_to_user(buf, fifo->z_base + *Z2_F2(fifo), size))
			return -EFAULT;
	} else {
		// FIFO wrap

		if (copy_to_user(buf, fifo->z_base + *Z2_F2(fifo), octets_to_boundary))
			return -EFAULT;

		if (copy_to_user(buf + octets_to_boundary,
				fifo->fifo_base,
				size - octets_to_boundary))
			return -EFAULT;
	}

	return 0;
}

int hfc_fifo_mem_write_user(
	struct hfc_fifo *fifo,
	const void __user *buf,
	int size)
{
	int octets_to_boundary = fifo->z_max - *Z1_F1(fifo) + 1;
	if (octets_to_boundary >= size) {
		if (copy_from_user(fifo->z_base + *Z1_F1(fifo), buf, size))
			return -EFAULT;
	} else {
		// FIFO wrap

		if (copy_from_user(fifo->z_base + *Z1_F1(fifo),
				buf,
				octets_to_boundary))
			return -EFAULT;

		if (copy_from_user(fifo->fifo_base,
				buf + octets_to_boundary,
				size - octets_to_boundary))
			return -EFAULT;
	}

	return 0;
}

void hfc_fifo_drop(struct hfc_fifo *fifo, int size)
{
	int available_octets = hfc_fifo_used_rx(fifo);
	if (available_octets + 1 < size) {
		hfc_msg_fifo(fifo, KERN_WARNING,
			"RX FIFO not enough (%d) octets to drop!\n",
			available_octets);

		return;
	}

	*Z2_F2(fifo) = Z_inc(fifo, *Z2_F2(fifo), size);
}

int hfc_fifo_get_frame(struct hfc_fifo *fifo, void *data, int max_size)
{

	if (*fifo->f1 == *fifo->f2) {
		// nothing received, strange uh?
		hfc_msg_fifo(fifo, KERN_WARNING,
			"get_frame called with no frame in FIFO.\n");

		return -1;
	}

	// frame_size includes CRC+CRC+STAT
	int frame_size = hfc_fifo_get_frame_size(fifo);

#ifdef DEBUG
	if(debug_level == 3) {
		hfc_msg_fifo(fifo, KERN_DEBUG,
			"RX len %2d: ",
			frame_size);
	} else if(debug_level >= 4) {
		hfc_msg_fifo(fifo, KERN_DEBUG,
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			*fifo->f1, *fifo->f2, *Z1_F2(fifo), *Z2_F2(fifo),
			frame_size);
	}

	if(debug_level >= 3) {
		int i;
		for (i=0; i < frame_size; i++) {
			printk("%02x", hfc_fifo_u8(fifo,
				Z_inc(fifo, *Z2_F2(fifo), i)));
		}

		printk("\n"); 
	}
#endif

	if (frame_size <= 0) {
#ifdef DEBUG
		if (debug_level >= 2) {
			hfc_msg_fifo(fifo, KERN_DEBUG,
				"invalid (empty) frame received.\n");
		}
#endif

		hfc_fifo_drop_frame(fifo);
		return -1;
	}

	// STAT is not really received
//	fifo->bytes += frame_size - 1;

	// Calculate beginning of the next frame
	u16 newz2 = Z_inc(fifo, *Z2_F2(fifo), frame_size);

	// We cannot use hfc_fifo_get because of different semantic of
	// "available bytes" and to avoid useless increment of Z2
	hfc_fifo_mem_read(fifo, *Z2_F2(fifo), data,
		frame_size < max_size ? frame_size : max_size);

	if (hfc_fifo_u8(fifo, Z_inc(fifo, *Z2_F2(fifo),
		frame_size - 1)) != 0x00) {
		// CRC not ok, frame broken, skipping
#ifdef DEBUG
		if(debug_level >= 2) {
			hfc_msg_fifo(fifo, KERN_WARNING,
				"Received frame with wrong CRC\n");
		}
#endif

//		fifo->crc++;
//		fifo->fifo->net_device_stats.rx_errors++;

		hfc_fifo_drop_frame(fifo);
		return -1;
	}

//	fifo->frames++;

	*fifo->f2 = F_inc(fifo, *fifo->f2, 1);

	// Set Z2 for the next frame we're going to receive
	*Z2_F2(fifo) = newz2;

	return frame_size;
}

void hfc_fifo_drop_frame(struct hfc_fifo *fifo)
{

	if (*fifo->f1 == *fifo->f2) {
		// nothing received, strange eh?
		hfc_msg_fifo(fifo, KERN_WARNING,
			"skip_frame called with no frame in FIFO.\n");

		return;
	}

//	fifo->drops++;

	int available_octets = hfc_fifo_used_rx(fifo) + 1;

	// Calculate beginning of the next frame
	u16 newz2 = Z_inc(fifo, *Z2_F2(fifo), available_octets);

	*fifo->f2 = F_inc(fifo, *fifo->f2, 1);

	// Set Z2 for the next frame we're going to receive
	*Z2_F2(fifo) = newz2;
}

void hfc_fifo_put_frame(struct hfc_fifo *fifo,
		 void *data, int size)
{
#ifdef DEBUG
	if (debug_level == 3) {
		hfc_msg_fifo(fifo, KERN_DEBUG,
			"TX len %2d: ",
			size);
	} else if (debug_level >= 4) {
		hfc_msg_fifo(fifo, KERN_DEBUG,
			"TX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			*fifo->f1, *fifo->f2, *Z1_F1(fifo), *Z2_F1(fifo),
			size);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<size; i++)
			printk("%02x",((u8 *)data)[i]);

		printk("\n");
	}
#endif

/*	int available_frames = hfc_fifo_free_frames(fifo);

	if (available_frames >= fifo->f_num) {
		hfc_msg_fifo(fifo, KERN_CRIT,
			"TX FIFO total number of frames exceeded!\n");

//		fifo->fifo_full++;

		// FIFO RESET ?

		return;
	}*/

	hfc_fifo_mem_write(fifo, data, size);

	u16 newz1 = Z_inc(fifo, *Z1_F1(fifo), size);
	*Z1_F1(fifo) = newz1;
	*fifo->f1 = F_inc(fifo, *fifo->f1, 1);
	*Z1_F1(fifo) = newz1;

//	fifo->frames++;
}

void hfc_fifo_set_bit_order(struct hfc_fifo *fifo, int reversed)
{
	struct hfc_card *card = fifo->card;

	BUG_ON(!fifo->connected_chan);

	if (fifo->connected_chan->chan->id == B1) {
		if (reversed)
			card->regs.cirm |= hfc_CIRM_B1_REV;
		else
			card->regs.cirm &= ~hfc_CIRM_B1_REV;
	} else if (fifo->connected_chan->chan->id == B2) {
		if (reversed)
			card->regs.cirm |= hfc_CIRM_B2_REV;
		else
			card->regs.cirm &= ~hfc_CIRM_B2_REV;
	}

	hfc_outb(card, hfc_CIRM, card->regs.cirm);
}

void hfc_fifo_rx_work(void *data)
{
	struct hfc_fifo *fifo = data;

	WARN_ON(!fifo->connected_chan);
	if (!fifo->connected_chan)
		return;

	struct hfc_chan_simplex *chan = fifo->connected_chan;
	struct hfc_chan_duplex *fdchan = chan->chan;

	if (!hfc_fifo_has_frames(fifo))
		goto no_frames;

	int frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size < 3) {
		hfc_debug_fifo(fifo, 2,
			"invalid frame received, just %d octets\n",
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
		schedule_work(&fifo->work);
}

void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction)
{
	fifo->hw_index = hw_index;
	fifo->direction = direction;
	fifo->card = card;

	if (fifo->direction == RX)
		INIT_WORK(&fifo->work,
			hfc_fifo_rx_work,
			fifo);
}

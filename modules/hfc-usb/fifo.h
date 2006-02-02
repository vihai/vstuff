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

#ifndef _HFC_FIFO_H
#define _HFC_FIFO_H

#define FIFO_B1 0
#define FIFO_B2 1
#define FIFO_D 2
#define FIFO_PCME 3

enum hfc_fifo_flag
{
	HFC_FIFO_FLAG_WAITING_BUSY,
};

struct hfc_st_chan;
struct hfc_fifo
{
	struct hfc_card *card;
	struct hfc_st_chan *connected_chan;

	int hw_index;
	enum hfc_direction direction;

	const char *name;

	int subchannel_bit_count;
	int subchannel_bit_start;

	BOOL enabled;
	BOOL bit_reversed;
	BOOL framer_enabled;

	struct urb *urb;
	int pipe;

	struct usb_host_endpoint *int_endpoint;
	struct usb_host_endpoint *iso_endpoint;

	u8 urb_buf[128];
	u8 frame_buf[512];
	int frame_buf_pos;
	int frame_buf_len;

	unsigned long flags;
};

int hfc_fifo_xmit(struct hfc_fifo *fifo, void *buf, int len);

int hfc_fifo_is_running(struct hfc_fifo *fifo);
void hfc_fifo_init(
	struct hfc_fifo *fifo,
	struct hfc_card *card,
	int hw_index,
	enum hfc_direction direction,
	const char *name);
int hfc_fifo_alloc(struct hfc_fifo *fifo);
void hfc_fifo_dealloc(struct hfc_fifo *fifo);
void hfc_fifo_reset(struct hfc_fifo *fifo);
void hfc_fifo_configure(struct hfc_fifo *fifo);

#endif

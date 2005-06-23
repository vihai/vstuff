/*
 *
 *
 * Copyright (C) 2004 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _HFC_USB_H
#define _HFC_USB_H

#include <linux/delay.h>
#include <linux/usb.h>

#include "regs.h"

#ifdef DEBUG
#define hfc_debug(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)		\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX format, ## arg)

#define hfc_debug_card(dbglevel, card, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			format, card->id, ## arg)

#define hfc_debug_chan(dbglevel, chan, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"chan: %s "			\
			format,				\
			chan->card->id,			\
			chan->name,			\
			## arg)

#define hfc_debug_schan(dbglevel, chan, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"chan: %s "			\
			format,				\
			chan->chan->card->id,		\
			chan->chan->name,		\
			## arg)
#else
#define hfc_debug(dbglevel, format, arg...) do {} while (0)
#define hfc_debug_card(dbglevel, card, format, arg...) do {} while (0)
#define hfc_debug_chan(dbglevel, chan, format, arg...) do {} while (0)
#define hfc_debug_schan(dbglevel, schan, format, arg...) do {} while (0)
#endif


#define hfc_msg(level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		format, ## arg)

#define hfc_msg_card(level, card, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		format, card->id, ## arg)

#define hfc_msg_chan(level, chan, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"chan: %s "				\
		format,					\
		chan->card->id,				\
		chan->name,				\
		## arg)

#define hfc_msg_schan(level, chan, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"chan: %s "				\
		format,					\
		chan->chan->card->id,			\
		chan->chan->name,			\
		## arg)


#define hfc_DRIVER_NAME "hfc-usb"
#define hfc_DRIVER_PREFIX hfc_DRIVER_NAME ": "
#define hfc_DRIVER_DESCR "HFC-USB Driver"

#define hfc_MAX_BOARDS 32

#ifndef intptr_t
#define intptr_t unsigned long
#endif

#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

#ifndef PCI_DMA_32BIT
#define PCI_DMA_32BIT	0x00000000ffffffffULL
#endif

#ifndef PCI_DEVICE_ID_CCD_08B4
#define PCI_DEVICE_ID_CCD_08B4		0x08b4
#endif

#ifndef PCI_DEVICE_ID_CCD_16B8
#define PCI_DEVICE_ID_CCD_16B8		0x16B8
#endif

#define hfc_CLKDEL_TE	0x0E
#define hfc_CLKDEL_NT	0x0C

#define hfc_PCI_MEM_SIZE	0x1000

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

struct hfc_chan_simplex {
	struct hfc_chan_duplex *chan;

	int fifo_id;

	struct urb *urb;
	int pipe;

	struct usb_host_endpoint *int_endpoint;
	struct usb_host_endpoint *iso_endpoint;

	u8 urb_buf[128]; // FIXME
	u8 frame_buf[128];
	int frame_buf_pos;

	int expecting_data;

	unsigned long long frames;
	unsigned long long bytes;
	unsigned long long fifo_full;
	unsigned long long crc;
};

enum hfc_chan_status {
	free,
	open_framed,
	open_voice,
	sniff_aux,
	loopback,
};

struct hfc_chan_duplex {
	struct hfc_card *card;

	char *name;
	int id;

	enum hfc_chan_status status;

	unsigned short protocol;

//	spinlock_t lock;

	struct hfc_chan_simplex rx;
	struct hfc_chan_simplex tx;

	struct net_device *netdev;
	struct net_device_stats net_device_stats;
};

typedef struct hfc_card {
	int id;
	spinlock_t lock;

	int ticks;

	struct usb_device *usb_dev;
	struct usb_interface *usb_interface;

	int pipe_in;
	int pipe_out;

	struct proc_dir_entry *proc_dir;
	char proc_dir_name[32];

	struct proc_dir_entry *proc_info;
	struct proc_dir_entry *proc_fifos;

	struct kref kref;



	int echo_enabled;
	int nt_mode;
	u8 l1_state;

	struct hfc_chan_duplex chans[4];



} hfc_card;
#define to_hfc_dev(d) container_of(d, struct hfc_card, kref)

extern int debug_level;

static inline void hfc_write(struct hfc_card *card, u8 reg, u8 value)
{
//	printk(KERN_DEBUG hfc_DRIVER_PREFIX "REG %02x = %02x\n", reg, value);

	usb_control_msg(card->usb_dev, card->pipe_out,
		0, 0x40, value, reg, NULL, 0, USB_CTRL_SET_TIMEOUT * HZ);
}

static inline u8 hfc_read(struct hfc_card *card, u8 reg)
{
	u8 value;

	usb_control_msg(card->usb_dev, card->pipe_in,
		1, 0xC0, 0, reg, &value, 1, USB_CTRL_GET_TIMEOUT * HZ);

	return value;
}

#endif

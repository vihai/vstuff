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

#ifndef _HFC_4S_H
#define _HFC_4S_H

#include <linux/delay.h>
#include <linux/ppp_channel.h>

#include "regs.h"

#define hfc_DRIVER_NAME "hfc-4s"
#define hfc_DRIVER_PREFIX hfc_DRIVER_NAME ": "
#define hfc_DRIVER_DESCR "HFC-4S HFC-8S Driver"

#define hfc_MAX_BOARDS 32

#ifdef DEBUG
#define hfc_debug(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)		\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX format, ## arg)

#define hfc_debug_card(dbglevel, card, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			format, card->id, ## arg)

#define hfc_debug_port(dbglevel, port, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"port: %d "			\
			format,				\
			port->card->id, port->id, ## arg)

#define hfc_debug_chan(dbglevel, chan, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"port: %d "			\
			"chan: %s "			\
			format,				\
			chan->port->card->id,		\
			chan->port->id,			\
			chan->name,			\
			## arg)

#define hfc_debug_schan(dbglevel, chan, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"port: %d "			\
			"chan: %s "			\
			format,				\
			chan->chan->port->card->id,	\
			chan->chan->port->id,		\
			chan->chan->name,		\
			## arg)
#else
#define hfc_debug(dbglevel, format, arg...) do {} while (0)
#define hfc_debug_card(dbglevel, card, format, arg...) do {} while (0)
#define hfc_debug_port(dbglevel, port, format, arg...) do {} while (0)
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

#define hfc_msg_port(level, port, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"port: %d "				\
		format,					\
		port->card->id, port->id, ## arg)

#define hfc_msg_chan(level, chan, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"port: %d "				\
		"chan: %s "				\
		format,					\
		chan->port->card->id,			\
		chan->port->id,				\
		chan->name,				\
		## arg)

#define hfc_msg_schan(level, chan, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"port: %d "				\
		"chan: %s "				\
		format,					\
		chan->chan->port->card->id,		\
		chan->chan->port->id,			\
		chan->chan->name,			\
		## arg)

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

#define D 0
#define B1 1
#define B2 2
#define E 3

#define D_FIFO_OFF 2
#define B1_FIFO_OFF 0
#define B2_FIFO_OFF 1
#define E_FIFO_OFF 3

struct hfc_chan;

union hfc_zgroup
{
	struct { u16 z1,z2; };
	u32 z1z2;
};

union hfc_fgroup
{
	struct { u8 f1,f2; };
	u16 f1f2;
};

struct hfc_chan_simplex {
	struct hfc_chan_duplex *chan;

	int fifo_id;

	// Counters cache
	union {
		struct { u16 z1,z2; };
		u32 z1z2;
	};

	union {
		struct { u8 f1,f2; };
		u16 f1f2;
	};

	u16 z_min;
	u16 z_max;
	u16 fifo_size;

	u8 f_min;
	u8 f_max;
	u8 f_num;

	unsigned long long frames;
	unsigned long long bytes;
	unsigned long long fifo_full;
	unsigned long long crc;
};

enum hfc_chan_status {
	free,
	open_lapd,
	open_ppp,
	open_voice,
	sniff_aux,
	loopback,
};

struct hfc_port;
struct hfc_chan_duplex {
	struct hfc_port *port;

	char *name;
	int id;

	enum hfc_chan_status status;

	unsigned short protocol;

	struct hfc_chan_simplex rx;
	struct hfc_chan_simplex tx;

	struct net_device *netdev;
	struct net_device_stats net_device_stats;

	struct ppp_channel ppp_chan;
};

struct hfc_port
{
	struct hfc_card *card;

	int id;

	struct hfc_chan_duplex chans[4];
	int echo_enabled;
	int nt_mode;
	u8 l1_state;

	struct
	{
		u8 st_ctrl_0;
		u8 st_ctrl_2;
	} regs;
};

enum hfc_chip_type
{
	HFC_CHIPTYPE_4S,
	HFC_CHIPTYPE_8S,
};

typedef struct hfc_card {
	int id;
	spinlock_t lock;

	int ticks;

	struct pci_dev *pcidev;

	struct proc_dir_entry *proc_dir;
	char proc_dir_name[32];

	struct proc_dir_entry *proc_info;
	struct proc_dir_entry *proc_fifos;

	enum hfc_chip_type chip_type;
	int num_ports;
	struct hfc_port ports[8];

	unsigned long io_bus_mem;
	void *io_mem;

	int sync_loss_reported;
	int late_irqs;

	int ignore_first_timer_interrupt;

	int open_ports;
} hfc_card;

static inline u8 hfc_inb(struct hfc_card *card, int offset)
{
 return readb(card->io_mem + offset);
}

static inline void hfc_outb(struct hfc_card *card, int offset, u8 value)
{
 writeb(value, card->io_mem + offset);
}

static inline u16 hfc_inw(struct hfc_card *card, int offset)
{
 return readw(card->io_mem + offset);
}

static inline void hfc_outw(struct hfc_card *card, int offset, u16 value)
{
 writew(value, card->io_mem + offset);
}

static inline u32 hfc_inl(struct hfc_card *card, int offset)
{
 return readl(card->io_mem + offset);
}

static inline void hfc_outl(struct hfc_card *card, int offset, u32 value)
{
 writel(value, card->io_mem + offset);
}

static inline void hfc_wait_busy(struct hfc_card *card)
{
	int i;
	for (i=0; i<1000; i++) {
		if (!(hfc_inb(card, hfc_R_STATUS) & hfc_R_STATUS_V_BUSY))
			return;

		udelay(1);
	}

	printk(KERN_ERR hfc_DRIVER_PREFIX
		"card %d: "
		"card is stuck in busy state...\n",
		card->id);
}

extern int debug_level;

#endif

/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include <linux/visdn/core.h>
#include <linux/visdn/chan.h>
#include <linux/visdn/softcxc.h>

#include "card.h"
#include "st_chan.h"
#include "st_port.h"
#include "fifo.h"
#include "fifo_inline.h"

#define HFC_FIFO_JITTBUFF 5

#define to_st_chan(chan) \
	container_of(chan, struct hfc_st_chan, visdn_chan)

#ifdef DEBUG_CODE
#define hfc_debug_chan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pci_dev->dev.bus->name,	\
			(chan)->port->card->pci_dev->dev.bus_id,	\
			(chan)->visdn_chan.name,			\
			## arg)
#else
#define hfc_debug_chan(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"st:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pci_dev->dev.bus->name,		\
		(chan)->port->card->pci_dev->dev.bus_id,		\
		(chan)->visdn_chan.name,				\
		## arg)

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_size(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->rx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->rx_fifo->size);
	else
		return snprintf(buf, PAGE_SIZE, "\n");

}

static VISDN_CHAN_ATTR(rx_fifo_size, S_IRUGO,
		hfc_show_rx_fifo_size,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->rx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				hfc_fifo_used_rx(chan->rx_fifo));
	else
		return snprintf(buf, PAGE_SIZE, "\n");

}

static ssize_t hfc_store_rx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	hfc_card_lock(chan->port->card);
	if (chan->rx_fifo)
		hfc_fifo_reset(chan->rx_fifo);
	hfc_card_unlock(chan->port->card);

	return count;
}

static VISDN_CHAN_ATTR(rx_fifo_used, S_IRUGO | S_IWUSR,
		hfc_show_rx_fifo_used,
		hfc_store_rx_fifo_used);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_min(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->rx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->rx_fifo->stats_min);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static VISDN_CHAN_ATTR(rx_fifo_min, S_IRUGO,
		hfc_show_rx_fifo_min, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_max(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->rx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->rx_fifo->stats_max);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static VISDN_CHAN_ATTR(rx_fifo_max, S_IRUGO,
		hfc_show_rx_fifo_max, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_size(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->tx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->tx_fifo->size);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static VISDN_CHAN_ATTR(tx_fifo_size, S_IRUGO,
		hfc_show_tx_fifo_size,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->tx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				hfc_fifo_used_tx(chan->tx_fifo));
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t hfc_store_tx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	hfc_card_lock(chan->port->card);
	if (chan->tx_fifo)
		hfc_fifo_reset(chan->tx_fifo);
	hfc_card_unlock(chan->port->card);

	return count;
}

static VISDN_CHAN_ATTR(tx_fifo_used, S_IRUGO | S_IWUSR,
		hfc_show_tx_fifo_used,
		hfc_store_tx_fifo_used);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_min(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->tx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->tx_fifo->stats_min);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static VISDN_CHAN_ATTR(tx_fifo_min, S_IRUGO,
		hfc_show_tx_fifo_min, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_max(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	if (chan->tx_fifo)
		return snprintf(buf, PAGE_SIZE, "%d\n",
				chan->tx_fifo->stats_max);
	else
		return snprintf(buf, PAGE_SIZE, "\n");
}

static VISDN_CHAN_ATTR(tx_fifo_max, S_IRUGO,
		hfc_show_tx_fifo_max, NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int bits = hfc_SQ_REC_BITS(hfc_inb(card, hfc_SQ_REC));

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	hfc_outb(card, hfc_SQ_SEND, hfc_SQ_SEND_BITS(value));

	return count;
}

static VISDN_CHAN_ATTR(sq_bits, S_IRUGO | S_IWUSR,
		hfc_show_sq_bits,
		hfc_store_sq_bits);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->sq_enabled ? 1 : 0));

}

static ssize_t hfc_store_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->sq_enabled = !!value;
	hfc_st_port_update_sctrl(port),
	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);

static struct visdn_chan_attribute *hfc_st_attributes[] =
{
	&visdn_chan_attr_rx_fifo_size,
	&visdn_chan_attr_rx_fifo_used,
	&visdn_chan_attr_rx_fifo_min,
	&visdn_chan_attr_rx_fifo_max,

	&visdn_chan_attr_tx_fifo_size,
	&visdn_chan_attr_tx_fifo_used,
	&visdn_chan_attr_tx_fifo_min,
	&visdn_chan_attr_tx_fifo_max,
	NULL
};

static int hfc_st_chan_sysfs_create_files_DB(
	struct hfc_st_chan *chan)
{
	struct visdn_chan_attribute **attr = hfc_st_attributes;

	while(*attr) {
		visdn_chan_create_file(
			&chan->visdn_chan,
			*attr);

		attr++;
	}

	return 0;
}

int hfc_st_chan_sysfs_create_files_D(
	struct hfc_st_chan *chan)
{
	return hfc_st_chan_sysfs_create_files_DB(chan);
}

int hfc_st_chan_sysfs_create_files_E(
	struct hfc_st_chan *chan)
{
	return 0;
}

int hfc_st_chan_sysfs_create_files_B(
	struct hfc_st_chan *chan)
{
	return hfc_st_chan_sysfs_create_files_DB(chan);
}

int hfc_st_chan_sysfs_create_files_SQ(
	struct hfc_st_chan *chan)
{
	int err;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_bits);
	if (err < 0)
		goto err_create_file_sq_bits;

	err = visdn_chan_create_file(
		&chan->visdn_chan,
		&visdn_chan_attr_sq_enabled);
	if (err < 0)
		goto err_create_file_sq_enabled;

	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_enabled);
err_create_file_sq_enabled:
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_bits);
err_create_file_sq_bits:

	return err;
}

void hfc_st_chan_sysfs_delete_files_DB(
	struct hfc_st_chan *chan)
{
	struct visdn_chan_attribute **attr = hfc_st_attributes;

	while(*attr) {
		visdn_chan_remove_file(
			&chan->visdn_chan,
			*attr);

		attr++;
	}
}

void hfc_st_chan_sysfs_delete_files_D(
	struct hfc_st_chan *chan)
{
	hfc_st_chan_sysfs_delete_files_DB(chan);
}

void hfc_st_chan_sysfs_delete_files_E(
	struct hfc_st_chan *chan)
{
}

void hfc_st_chan_sysfs_delete_files_B(
	struct hfc_st_chan *chan)
{
	hfc_st_chan_sysfs_delete_files_DB(chan);
}

void hfc_st_chan_sysfs_delete_files_SQ(
	struct hfc_st_chan *chan)
{
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_enabled);
	visdn_chan_remove_file(&chan->visdn_chan, &visdn_chan_attr_sq_bits);
}


static void hfc_st_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_st_chan_release()\n");

	// FIXME
}

static int hfc_st_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int err;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->visdn_chan.leg_a.framing != VISDN_LEG_FRAMING_NONE &&
	    chan->visdn_chan.leg_a.framing != VISDN_LEG_FRAMING_HDLC) {
		hfc_debug_chan(chan, 1,
			"open failed: unsupported framing %d\n",
			chan->visdn_chan.leg_a.framing);
		err = -EINVAL;
		goto err_invalid_framing;
	}

	switch(chan->id) {
	case D:
		chan->rx_fifo = &card->fifos[hfc_FIFO_D][RX];
		chan->tx_fifo = &card->fifos[hfc_FIFO_D][TX];
	break;

	case B1:
		chan->rx_fifo = &card->fifos[hfc_FIFO_B1][RX];
		chan->tx_fifo = &card->fifos[hfc_FIFO_B1][TX];
	break;

	case B2:
		chan->rx_fifo = &card->fifos[hfc_FIFO_B2][RX];
		chan->tx_fifo = &card->fifos[hfc_FIFO_B2][TX];
	break;

	case E:
		chan->rx_fifo = &card->fifos[hfc_FIFO_B2][RX];
		chan->tx_fifo = &card->fifos[hfc_FIFO_B2][TX];
	break;
	default:
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	if ((chan->rx_fifo && chan->rx_fifo->connected_chan) ||
	    (chan->tx_fifo && chan->tx_fifo->connected_chan)) {
		err = -EBUSY;
		goto err_fifo_busy;
	}

	if (chan->rx_fifo) {
		chan->rx_fifo->connected_chan = chan;
		chan->rx_fifo->enabled = TRUE;

		if (chan->visdn_chan.leg_a.framing == VISDN_LEG_FRAMING_NONE) {
			chan->rx_fifo->framer_enabled = FALSE;
			chan->rx_fifo->bit_reversed = TRUE;

		} else if (chan->visdn_chan.leg_a.framing ==
						VISDN_LEG_FRAMING_HDLC) {
			chan->rx_fifo->framer_enabled = TRUE;
			chan->rx_fifo->bit_reversed = FALSE;
		}

		hfc_fifo_reset(chan->rx_fifo);
		hfc_fifo_configure(chan->rx_fifo);
	}

	if (chan->tx_fifo) {
		chan->tx_fifo->connected_chan = chan;
		chan->tx_fifo->enabled = TRUE;

		if (chan->visdn_chan.leg_a.framing == VISDN_LEG_FRAMING_NONE) {
			chan->tx_fifo->framer_enabled = FALSE;
			chan->tx_fifo->bit_reversed = TRUE;
		} else if (chan->visdn_chan.leg_a.framing ==
						VISDN_LEG_FRAMING_HDLC) {
			chan->tx_fifo->framer_enabled = TRUE;
			chan->tx_fifo->bit_reversed = FALSE;
		}

		hfc_fifo_reset(chan->tx_fifo);
		hfc_fifo_configure(chan->tx_fifo);
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_msg_chan(chan, KERN_INFO, "channel opened\n");

	return 0;

err_invalid_framing:
err_fifo_busy:
err_invalid_chan:
	chan->tx_fifo = NULL;
	chan->rx_fifo = NULL;
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static int hfc_st_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->rx_fifo) {
		chan->rx_fifo->enabled = FALSE;

		hfc_fifo_reset(chan->rx_fifo);
		hfc_fifo_configure(chan->rx_fifo);

		chan->rx_fifo->connected_chan = NULL;
		chan->rx_fifo = NULL;
	}

	if (chan->tx_fifo) {
		chan->tx_fifo->enabled = FALSE;

		hfc_fifo_reset(chan->tx_fifo);
		hfc_fifo_configure(chan->tx_fifo);

		chan->tx_fifo->connected_chan = NULL;
		chan->tx_fifo = NULL;
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_chan(chan, 1, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
}

static int hfc_st_chan_start(struct visdn_chan *visdn_chan)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	if (chan->rx_fifo)
		hfc_fifo_reset(chan->rx_fifo);

	if (chan->tx_fifo) {
		hfc_fifo_reset(chan->tx_fifo);

		if (!chan->tx_fifo->framer_enabled) {
			int i;
			for (i=0; i<HFC_FIFO_JITTBUFF; i++) {
				u8 foo = 0;
				hfc_fifo_mem_write(chan->tx_fifo, &foo, 1);
			}
		}
	}

	if (chan->id == E) {
		card->regs.trm |= hfc_TRM_ECHO;
		hfc_outb(card, hfc_TRM, card->regs.trm);
	}

	hfc_st_port_update_sctrl(chan->port);
	hfc_st_port_update_sctrl_r(chan->port);

	hfc_card_unlock(card);

	hfc_msg_chan(chan, KERN_INFO, "channel started\n");

	return 0;
}

static int hfc_st_chan_stop(struct visdn_chan *visdn_chan)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	if (chan->id == E) {
		card->regs.trm &= ~hfc_TRM_ECHO;
		hfc_outb(card, hfc_TRM, card->regs.trm);
	}

	if (chan->rx_fifo)
		hfc_fifo_reset(chan->rx_fifo);

	if (chan->tx_fifo)
		hfc_fifo_reset(chan->tx_fifo);

	hfc_st_port_update_sctrl(chan->port);
	hfc_st_port_update_sctrl_r(chan->port);

	hfc_card_unlock(card);

	hfc_debug_chan(chan, 1, "channel stopped\n");

	return 0;
}

static int hfc_st_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	struct hfc_fifo *fifo = chan->tx_fifo;

	hfc_card_lock(card);

	if (!fifo)
		goto no_fifo;

	if (hfc_fifo_free_frames(fifo) <= 1) {
		hfc_debug_chan(chan, 3, "TX FIFO frames full, throttling\n");

		visdn_leg_stop_queue(&chan->visdn_chan.leg_a);
	}

	if (hfc_fifo_free_tx(fifo) < skb->len) {
		hfc_debug_chan(chan, 3, "TX FIFO full, throttling\n");

		visdn_leg_stop_queue(&chan->visdn_chan.leg_a);

		visdn_leg_tx_error(&chan->visdn_chan.leg_a,
			VISDN_TX_ERROR_FIFO_FULL);

		goto err_no_free_tx;
	}

#ifdef DEBUG_CODE
	if (debug_level == 3) {
		hfc_debug_chan(chan, 3, "TX len %2d: ", skb->len);

	} else if (debug_level >= 4) {
		hfc_debug_chan(chan, 4,
			"TX (f1=%02x, f2=%02x, z1=%04x z2=%04x) len %2d: ",
			*fifo->f1, *fifo->f2,
			Z1_F1(fifo), Z2_F1(fifo),
			skb->len);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<skb->len; i++)
			printk("%02x",((u8 *)skb->data)[i]);

		printk("\n");
	}
#endif

	hfc_fifo_mem_write(fifo, skb->data, skb->len);

	{
	// Move Z1 and jump to next frame
	u16 newz1 = Z_inc(fifo, Z1_F1(fifo), skb->len);
	set_Z1_F1(fifo, newz1);
	*fifo->f1 = F_inc(fifo, *fifo->f1, 1);
	set_Z1_F1(fifo, newz1);
	}

no_fifo:
	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	return VISDN_TX_OK;

err_no_free_tx:

	hfc_card_unlock(card);

	return VISDN_TX_BUSY;
}

void hfc_st_chan_rx_work(void *data)
{
	struct hfc_st_chan *chan = data;
	struct hfc_card *card = chan->port->card;
	struct hfc_fifo *fifo = chan->rx_fifo;
	struct sk_buff *skb;
	int frame_size;
	u16 newz2;
	struct { u8 crc[2], stat; } __attribute((packed)) stat;

	hfc_card_lock(card);

	if (!fifo)
		goto err_no_fifo;

	if (!hfc_fifo_has_frames(fifo))
		goto no_frames;

	// frame_size includes CRC+CRC+STAT
	frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size < 3) {
		hfc_debug_chan(chan, 3,
			"invalid frame received, just %d octets\n",
			frame_size);

		visdn_leg_rx_error(&chan->visdn_chan.leg_a,
			VISDN_RX_ERROR_LENGTH);

		goto err_invalid_frame;

	} else if(frame_size == 3) {
		hfc_debug_chan(chan, 3,
			"empty frame received\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_a,
			VISDN_RX_ERROR_LENGTH);

		goto err_empty_frame;
	}

	skb = visdn_alloc_skb(frame_size - 3);
	if (!skb) {
		hfc_msg_chan(chan, KERN_ERR,
			"cannot allocate skb: frame dropped\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_a,
			VISDN_RX_ERROR_DROPPED);

		goto err_alloc_skb;
	}

	/* Calculate beginning of the next frame */
	newz2 = Z_inc(fifo, Z2_F2(fifo), frame_size);

	/* We cannot use hfc_fifo_get because of different semantic of
	 * "available bytes" and to avoid useless increment of Z2
	 */
	hfc_fifo_mem_read(fifo,
		skb_put(skb, frame_size - 3),
		frame_size - 3);

	hfc_fifo_mem_read_z(fifo, Z_inc(fifo, Z2_F2(fifo), frame_size - 3),
		&stat, sizeof(stat));

#ifdef DEBUG_CODE
	if(debug_level == 3) {
		hfc_msg_chan(chan, KERN_DEBUG,
			"RX len %2d: ",
			frame_size);
	} else if(debug_level >= 4) {
		hfc_msg_chan(chan, KERN_DEBUG,
			"RX (f1=%02x, f2=%02x, z1=%04x, z2=%04x) len %2d: ",
			*fifo->f1, *fifo->f2, Z1_F2(fifo), Z2_F2(fifo),
			frame_size);
	}

	if(debug_level >= 3) {
		int i;
		for (i=0; i < frame_size; i++) {
			printk("%02x", hfc_fifo_u8(fifo,
				Z_inc(fifo, Z2_F2(fifo), i)));
		}

		printk("\n");
	}
#endif

	if (stat.stat == 0xff) {
		// Frame abort detected

		hfc_debug_chan(chan, 3, "Frame abort detected\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_a,
			VISDN_RX_ERROR_FR_ABORT);

		goto err_frame_abort;

	} else if (stat.stat != 0x00) {
		// CRC not ok, frame broken, skipping

		hfc_debug_chan(chan, 2, "Received frame with wrong CRC\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_a,
			VISDN_RX_ERROR_CRC);

		goto err_crc_error;
	}

	*fifo->f2 = F_inc(fifo, *fifo->f2, 1);

	// Set Z2 for the next frame we're going to receive
	set_Z2_F2(fifo, newz2);

	visdn_leg_frame_xmit(&chan->visdn_chan.leg_a, skb);

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
		schedule_work(&chan->rx_work);

err_no_fifo:
	hfc_card_unlock(card);
}


static ssize_t hfc_st_chan_read(
	struct visdn_leg *visdn_leg,
	void *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;
	int err;

	hfc_card_lock(card);

	if (!chan->rx_fifo) {
		err = -EINVAL;
		goto err_no_fifo;
	}

	available_octets = hfc_fifo_used_rx(chan->rx_fifo);

	copied_octets = available_octets < count ? available_octets : count;

	if (available_octets > chan->rx_fifo->stats_max)
		chan->rx_fifo->stats_max = available_octets;

	if (available_octets - copied_octets < chan->rx_fifo->stats_min)
		chan->rx_fifo->stats_min = available_octets - copied_octets;

	chan->rx_fifo->stats_cycles++;

	hfc_fifo_mem_read(chan->rx_fifo, buf, copied_octets);

	set_Z2_F2(chan->rx_fifo, Z_inc(chan->rx_fifo,
					Z2_F2(chan->rx_fifo),
					copied_octets));

	hfc_card_unlock(card);

	return copied_octets;

err_no_fifo:

	hfc_card_unlock(card);

	return err;
}

static ssize_t hfc_st_chan_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;
	int used_octets;
	int err;

	hfc_card_lock(card);

	if (!chan->tx_fifo) {
		err = -EINVAL;
		goto err_no_fifo;
	}

	available_octets = hfc_fifo_free_tx(chan->tx_fifo);

	copied_octets = available_octets > count ? count : available_octets;

	used_octets = hfc_fifo_used_tx(chan->tx_fifo);

	chan->tx_fifo->stats_cycles++;
	if (chan->tx_fifo->stats_cycles >= 10) {
		if (chan->tx_fifo->stats_min < HFC_FIFO_JITTBUFF) {
			u8 foo = ((u8 *)buf)[0];
			hfc_fifo_mem_write(chan->tx_fifo, &foo, 1);
//			printk(KERN_DEBUG "Added one sample\n");
		}

		chan->tx_fifo->stats_cycles = 0;
		chan->tx_fifo->stats_min = INT_MAX;
		chan->tx_fifo->stats_max = 0;
	}

	if (used_octets > HFC_FIFO_JITTBUFF * 2) {
//		printk(KERN_DEBUG "Trimming output fifo\n");
		goto not_copying;
	}

	if (used_octets < chan->tx_fifo->stats_min)
		chan->tx_fifo->stats_min = used_octets;

	if (used_octets + copied_octets > chan->tx_fifo->stats_max)
		chan->tx_fifo->stats_max = used_octets + copied_octets;

	hfc_fifo_mem_write(chan->tx_fifo, buf, copied_octets);

	set_Z1_F1(chan->tx_fifo, Z_inc(chan->tx_fifo,
					Z1_F1(chan->tx_fifo),
					count));

not_copying:
	hfc_card_unlock(card);

	return copied_octets;

err_no_fifo:

	hfc_card_unlock(card);

	return err;
}

static int hfc_st_chan_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "connecting to %06d\n",
		visdn_leg2->chan->id);

	return 0;
}

static void hfc_st_chan_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_st_chan *chan = to_st_chan(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "disconnected from %06d\n",
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops hfc_st_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_st_chan_release,
	.open		= hfc_st_chan_open,
	.close		= hfc_st_chan_close,
	.start		= hfc_st_chan_start,
	.stop		= hfc_st_chan_stop,
};

static struct visdn_leg_ops hfc_leg_ops =
{
	.owner		= THIS_MODULE,

	.connect	= hfc_st_chan_connect,
	.disconnect	= hfc_st_chan_disconnect,

	.frame_xmit	= hfc_st_chan_frame_xmit,

	.read		= hfc_st_chan_read,
	.write		= hfc_st_chan_write,
};

void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id)
{
	chan->port = port;
	chan->id = id;

	visdn_chan_init(&chan->visdn_chan);
	chan->visdn_chan.ops = &hfc_st_chan_ops;
	chan->visdn_chan.chan_class = NULL;
	chan->visdn_chan.port = &port->visdn_port;

	chan->visdn_chan.leg_a.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_a.ops = &hfc_leg_ops;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE |
						VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_a.mtu = 0;

	chan->visdn_chan.leg_b.cxc = NULL;
	chan->visdn_chan.leg_b.ops = NULL;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.mtu = -1;

	strncpy(chan->visdn_chan.name, name, sizeof(chan->visdn_chan.name));
	chan->visdn_chan.driver_data = chan;

	INIT_WORK(&chan->rx_work, hfc_st_chan_rx_work, chan);
}

int hfc_st_chan_register(struct hfc_st_chan *chan)
{
	int err;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	switch(chan->id) {
	case D:
		hfc_st_chan_sysfs_create_files_D(chan);
	break;

	case B1:
	case B2:
		hfc_st_chan_sysfs_create_files_B(chan);
	break;

	case E:
		hfc_st_chan_sysfs_create_files_E(chan);
	break;

	case SQ:
		hfc_st_chan_sysfs_create_files_SQ(chan);
	break;
	}

	return 0;

err_chan_register:

	return err;
}

void hfc_st_chan_unregister(struct hfc_st_chan *chan)
{
	switch(chan->id) {
	case D:
		hfc_st_chan_sysfs_delete_files_D(chan);
	break;

	case B1:
	case B2:
		hfc_st_chan_sysfs_delete_files_B(chan);
	break;

	case E:
		hfc_st_chan_sysfs_delete_files_E(chan);
	break;

	case SQ:
		hfc_st_chan_sysfs_delete_files_SQ(chan);
	break;
	}

	visdn_chan_unregister(&chan->visdn_chan);
}

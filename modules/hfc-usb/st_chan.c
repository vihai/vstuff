/*
 * Cologne Chip's HFC-S USB vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi
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
#include "card_inline.h"
#include "st_chan.h"
#include "st_port.h"
#include "fifo.h"
#include "fifo_inline.h"

#define to_chan_duplex(chan) \
	container_of(chan, struct hfc_st_chan, visdn_chan)

#ifdef DEBUG_CODE
#define hfc_debug_chan(chan, dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"st:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->usb_dev->dev.bus->name,	\
			(chan)->port->card->usb_dev->dev.bus_id,	\
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
		(chan)->port->card->usb_dev->dev.bus->name,		\
		(chan)->port->card->usb_dev->dev.bus_id,		\
		(chan)->visdn_chan.name,				\
		## arg)

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
/*	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int bits = hfc_SQ_REC_BITS(hfc_inb(card, hfc_SQ_REC));*/
	int bits = 0;

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
/*	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	hfc_outb(card, hfc_SQ_SEND, hfc_SQ_SEND_BITS(value));
*/
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
	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->sq_enabled ? 1 : 0));

}

static ssize_t hfc_store_sq_enabled(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->sq_enabled = !!value;
//	hfc_st_port_update_sctrl(port),
	hfc_card_unlock(card);

	return count;
}

static VISDN_CHAN_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);


static int hfc_st_chan_sysfs_create_files_DB(
	struct hfc_st_chan *chan)
{
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
	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int err;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	chan->rx_fifo.enabled = TRUE;
	chan->tx_fifo.enabled = TRUE;

	if (chan->visdn_chan.leg_b.framing == VISDN_LEG_FRAMING_NONE) {
		chan->rx_fifo.framer_enabled = FALSE;
		chan->tx_fifo.framer_enabled = FALSE;
		chan->rx_fifo.bit_reversed = TRUE;
		chan->tx_fifo.bit_reversed = TRUE;
	} else if (chan->visdn_chan.leg_b.framing == VISDN_LEG_FRAMING_HDLC) {
		chan->rx_fifo.framer_enabled = TRUE;
		chan->tx_fifo.framer_enabled = TRUE;
		chan->rx_fifo.bit_reversed = FALSE;
		chan->tx_fifo.bit_reversed = FALSE;
	} else {
		hfc_debug_chan(chan, 1,
			"open failed: unsupported framing %d\n",
			chan->visdn_chan.leg_b.framing);
		err = -EINVAL;
		goto err_invalid_framing;
	}

	if (chan->id == B1) {
		card->leds[HFC_LED_B1].color = HFC_LED_ON;
		hfc_led_update(&card->leds[HFC_LED_B1]);
	} else if (chan->id == B2) {
		card->leds[HFC_LED_B2].color = HFC_LED_ON;
		hfc_led_update(&card->leds[HFC_LED_B2]);
	}

	hfc_fifo_select(&chan->rx_fifo);
	hfc_fifo_reset(&chan->rx_fifo);
	hfc_fifo_configure(&chan->rx_fifo);

	hfc_fifo_select(&chan->tx_fifo);
	hfc_fifo_reset(&chan->tx_fifo);
	hfc_fifo_configure(&chan->tx_fifo);

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	err = usb_submit_urb(chan->rx_fifo.urb, GFP_KERNEL);
	if (err < 0) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"usb_submit_urb() error %d\n", err);
		goto err_submit_urb;
	}

	hfc_msg_chan(chan, KERN_INFO, "channel opened.\n");

	return 0;

err_submit_urb:
err_invalid_framing:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static int hfc_st_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_st_chan *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	usb_kill_urb(chan->rx_fifo.urb);
	usb_kill_urb(chan->tx_fifo.urb);

	if (chan->id == B1) {
		card->leds[HFC_LED_B1].color = HFC_LED_OFF;
		hfc_led_update(&card->leds[HFC_LED_B1]);
	} else if (chan->id == B2) {
		card->leds[HFC_LED_B2].color = HFC_LED_OFF;
		hfc_led_update(&card->leds[HFC_LED_B2]);
	}

	chan->rx_fifo.enabled = FALSE;
	chan->tx_fifo.enabled = FALSE;

	hfc_fifo_select(&chan->rx_fifo);
	hfc_fifo_reset(&chan->rx_fifo);
	hfc_fifo_configure(&chan->rx_fifo);

	hfc_fifo_select(&chan->tx_fifo);
	hfc_fifo_reset(&chan->tx_fifo);
	hfc_fifo_configure(&chan->tx_fifo);

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_chan(chan, 1, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
}

static int hfc_st_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_leg->chan);
	int err;

	hfc_st_port_check_l1_up(chan->port);

	err = hfc_fifo_xmit(&chan->tx_fifo, skb->data, skb->len);
	if (err < 0)
		goto err_fifo_xmit;

	visdn_kfree_skb(skb);

	return VISDN_TX_OK;

err_fifo_xmit:
	visdn_leg_wake_queue(&chan->visdn_chan.leg_b);

	return err;
}

static ssize_t hfc_st_chan_read(
	struct visdn_leg *visdn_leg,
	void *buf,
	size_t count)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets = 0;

	hfc_card_lock(card);

#if 0
	int copied_octets = hfc_fifo_used_rx(&chan->rx_fifo);
	if (copied_octets > count)
		copied_octets = count;

	 hfc_fifo_mem_read(&chan->rx_fifo, buf, copied_octets);

	*Z2_F2(&chan->rx_fifo) = Z_inc(&chan->rx_fifo,
					*Z2_F2(&chan->rx_fifo),
					copied_octets);

#endif
	hfc_card_unlock(card);

	return copied_octets;
}

static ssize_t hfc_st_chan_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets = 0;

	hfc_card_lock(card);

#if 0
	int copied_octets = hfc_fifo_free_tx(&chan->tx_fifo);
	if (copied_octets > count)
		copied_octets = count;

	hfc_fifo_mem_write(&chan->tx_fifo, buf, copied_octets);

	*Z1_F1(&chan->tx_fifo) = Z_inc(&chan->tx_fifo,
					*Z1_F1(&chan->tx_fifo),
					count);
#endif
	hfc_card_unlock(card);

	return copied_octets;
}

static int hfc_st_chan_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "connecting to %06d\n",
		visdn_leg2->chan->id);

	return 0;
}

static void hfc_st_chan_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_st_chan *chan = to_chan_duplex(visdn_leg->chan);

	hfc_debug_chan(chan, 2, "disconnected from %06d\n",
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops hfc_st_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_st_chan_release,
	.open		= hfc_st_chan_open,
	.close		= hfc_st_chan_close,
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

static int hfc_st_chan_to_fifo_num(
	struct hfc_st_chan *chan,
	enum hfc_direction dir)
{
	switch (chan->id) {
	case D: return 4 + (dir == RX ? 1 : 0);
	case B1: return 0 + (dir == RX ? 1 : 0);
	case B2: return 2 + (dir == RX ? 1 : 0);
	case E: return 6 + (dir == RX ? 1 : 0);
	default:
		BUG();
		return 0;
	}
}

void hfc_st_chan_init(
	struct hfc_st_chan *chan,
	struct hfc_st_port *port,
	const char *name,
	int id,
	BOOL has_real_fifo,
	struct hfc_led *led,
	int subchannel_bit_count)
{
	chan->port = port;
	chan->id = id;

	chan->has_real_fifo = has_real_fifo;

	chan->led = led;

	visdn_chan_init(&chan->visdn_chan);
	chan->visdn_chan.ops = &hfc_st_chan_ops;
	chan->visdn_chan.chan_class = NULL;
	chan->visdn_chan.port = &port->visdn_port;

	chan->visdn_chan.leg_a.cxc = NULL;
	chan->visdn_chan.leg_a.ops = NULL;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_b.ops = &hfc_leg_ops;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE |
						VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_b.mtu = 128;

	strncpy(chan->visdn_chan.name, name, sizeof(chan->visdn_chan.name));
	chan->visdn_chan.driver_data = chan;

	if (has_real_fifo) {
		hfc_fifo_init(
			&chan->rx_fifo,
			chan, hfc_st_chan_to_fifo_num(chan, RX), RX,
			subchannel_bit_count);

		hfc_fifo_init(
			&chan->tx_fifo,
			chan, hfc_st_chan_to_fifo_num(chan, TX), TX,
			subchannel_bit_count);
	}
}

int hfc_st_chan_register(struct hfc_st_chan *chan)
{
	int err;

	if (chan->has_real_fifo) {
		err = hfc_fifo_alloc(&chan->rx_fifo);
		if (err < 0)
			goto err_fifo_rx_alloc;

		err = hfc_fifo_alloc(&chan->tx_fifo);
		if (err < 0)
			goto err_fifo_tx_alloc;
	}

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

	hfc_fifo_dealloc(&chan->tx_fifo);
err_fifo_tx_alloc:
	hfc_fifo_dealloc(&chan->rx_fifo);
err_fifo_rx_alloc:
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

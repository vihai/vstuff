/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
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
#include <linux/delay.h>

#include <linux/visdn/softcxc.h>

#include "card.h"
#include "sys_port.h"
#include "sys_chan.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "st_port_inline.h"

#define HFC_FIFO_JITTBUFF 5

#ifdef DEBUG_CODE
#define hfc_debug_sys_chan(chan, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"sys:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pci_dev->dev.bus->name,	\
			(chan)->port->card->pci_dev->dev.bus_id,	\
			(chan)->visdn_chan.name,			\
			## arg)

#else
#define hfc_debug_sys_chan(chan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_sys_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"sys:"							\
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
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->rx_fifo.size));

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
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);
	int fifo_used;

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->rx_fifo);
	fifo_used = hfc_fifo_used_rx(&chan->rx_fifo);
	hfc_card_unlock(chan->port->card);

	return snprintf(buf, PAGE_SIZE, "%d\n", fifo_used);
}

static ssize_t hfc_store_rx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->rx_fifo);
	hfc_fifo_reset(&chan->rx_fifo);
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
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n", chan->rx_fifo.stats_min);
}

static VISDN_CHAN_ATTR(rx_fifo_min, S_IRUGO,
		hfc_show_rx_fifo_min, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_max(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n", chan->rx_fifo.stats_max);
}

static VISDN_CHAN_ATTR(rx_fifo_max, S_IRUGO,
		hfc_show_rx_fifo_max, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_size(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->tx_fifo.size));

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
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	int fifo_used;

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->tx_fifo);
	fifo_used = hfc_fifo_used_rx(&chan->tx_fifo);
	hfc_card_unlock(chan->port->card);

	return snprintf(buf, PAGE_SIZE, "%d\n", fifo_used);
}

static ssize_t hfc_store_tx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->tx_fifo);
	hfc_fifo_reset(&chan->tx_fifo);
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
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n", chan->tx_fifo.stats_min);
}

static VISDN_CHAN_ATTR(tx_fifo_min, S_IRUGO,
		hfc_show_tx_fifo_min, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_fifo_max(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n", chan->tx_fifo.stats_max);
}

static VISDN_CHAN_ATTR(tx_fifo_max, S_IRUGO,
		hfc_show_tx_fifo_max, NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_subchannel_bit_start(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->rx_fifo.subchannel_bit_start);
}

static VISDN_CHAN_ATTR(rx_subchannel_bit_start, S_IRUGO,
		hfc_show_rx_subchannel_bit_start,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_subchannel_bit_start(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->tx_fifo.subchannel_bit_start);
}

static VISDN_CHAN_ATTR(tx_subchannel_bit_start, S_IRUGO,
		hfc_show_tx_subchannel_bit_start,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_subchannel_bit_count(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			chan->rx_fifo.subchannel_bit_count);
}

static VISDN_CHAN_ATTR(rx_subchannel_bit_count, S_IRUGO,
		hfc_show_rx_subchannel_bit_count,
		NULL);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_tx_subchannel_bit_count(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			chan->tx_fifo.subchannel_bit_count);
}

static VISDN_CHAN_ATTR(tx_subchannel_bit_count, S_IRUGO,
		hfc_show_tx_subchannel_bit_count,
		NULL);

/*---------------------------------------------------------------------------*/

static struct visdn_chan_attribute *hfc_sys_attributes[] =
{
	&visdn_chan_attr_rx_fifo_size,
	&visdn_chan_attr_rx_fifo_used,
	&visdn_chan_attr_rx_fifo_min,
	&visdn_chan_attr_rx_fifo_max,

	&visdn_chan_attr_tx_fifo_size,
	&visdn_chan_attr_tx_fifo_used,
	&visdn_chan_attr_tx_fifo_min,
	&visdn_chan_attr_tx_fifo_max,

	&visdn_chan_attr_rx_subchannel_bit_start,
	&visdn_chan_attr_tx_subchannel_bit_start,
	&visdn_chan_attr_rx_subchannel_bit_count,
	&visdn_chan_attr_tx_subchannel_bit_count,
	NULL
};

static void hfc_sys_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_sys_chan_release()\n");

	// FIXME
}

static int hfc_sys_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;
	int err;
	int i;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->connected_st_chan) {
		chan->rx_fifo.subchannel_bit_start =
			chan->connected_st_chan->subchannel_bit_start;
		chan->tx_fifo.subchannel_bit_start =
			chan->connected_st_chan->subchannel_bit_start;

		chan->rx_fifo.subchannel_bit_count =
			chan->connected_st_chan->subchannel_bit_count;
		chan->tx_fifo.subchannel_bit_count =
			chan->connected_st_chan->subchannel_bit_count;
	}

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
		err = -EINVAL;
		goto err_invalid_framing;
	}

	hfc_sys_port_upload_fsm(chan->port);

	/* RX FIFO */
	hfc_fifo_select(&chan->rx_fifo);
	hfc_fifo_reset(&chan->rx_fifo);
	hfc_fifo_configure(&chan->rx_fifo);

	/* TX FIFO */
	hfc_fifo_select(&chan->tx_fifo);
	hfc_fifo_reset(&chan->tx_fifo);
	hfc_fifo_configure(&chan->tx_fifo);

	if (!chan->tx_fifo.framer_enabled) {
		for (i=0; i<HFC_FIFO_JITTBUFF; i++) {
			u8 foo = 0;
			hfc_fifo_mem_write(&chan->tx_fifo, &foo, 1);
		}
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_sys_chan(chan, 1, "channel opened.\n");

	return 0;

err_invalid_framing:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "channel opening failed: %d\n", err);

	return err;
}

static int hfc_sys_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	chan->rx_fifo.enabled = FALSE;
	chan->tx_fifo.enabled = FALSE;

	hfc_fifo_select(&chan->rx_fifo);
	hfc_fifo_reset(&chan->rx_fifo);
	hfc_fifo_configure(&chan->rx_fifo);

	hfc_fifo_select(&chan->tx_fifo);
	hfc_fifo_reset(&chan->tx_fifo);
	hfc_fifo_configure(&chan->tx_fifo);

	hfc_sys_port_upload_fsm(chan->port);

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_sys_chan(chan, 1, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
}

static int hfc_sys_chan_leg_a_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_leg->chan);
	struct hfc_sys_port *port = chan->port;
	struct hfc_card *card = port->card;
	int err;

	hfc_debug_sys_chan(chan, 2, "leg A connecting to %s\n",
		visdn_leg2->chan->kobj.name);

	if (visdn_chan_lock_interruptible(visdn_leg->chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (visdn_leg2->chan->chan_class == &hfc_st_chan_class)
		chan->connected_st_chan = to_st_chan(visdn_leg2->chan);
	else if (visdn_leg2->chan->chan_class == &hfc_pcm_chan_class)
		chan->connected_pcm_chan = to_pcm_chan(visdn_leg2->chan);
	else
		WARN_ON(1);

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_leg->chan);

	return 0;

	visdn_chan_unlock(visdn_leg->chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static int hfc_sys_chan_leg_b_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_leg->chan);

	hfc_debug_sys_chan(chan, 2, "leg B connecting to %s\n",
		visdn_leg->chan->kobj.name);

	return 0;
}

static void hfc_sys_chan_leg_a_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
}

static void hfc_sys_chan_leg_b_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
}

static int hfc_sys_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	struct hfc_fifo *fifo = &chan->tx_fifo;

	hfc_card_lock(card);
	hfc_fifo_select(fifo);
	/*
	 * hfc_fifo_select() updates F/Z cache, so,
	 * size calculations are allowed
	 */

	if (hfc_fifo_free_frames(fifo) <= 1) {
		hfc_debug_sys_chan(chan, 3,
			"TX FIFO frames full, throttling\n");

		visdn_leg_stop_queue(&chan->visdn_chan.leg_b);
	}

	if (hfc_fifo_free_tx(fifo) < skb->len) {
		hfc_debug_sys_chan(chan, 3,
			"TX FIFO full (%d < %d), throttling\n",
			hfc_fifo_free_tx(fifo), skb->len);

		visdn_leg_stop_queue(&chan->visdn_chan.leg_b);

		visdn_leg_tx_error(&chan->visdn_chan.leg_b,
				VISDN_TX_ERROR_FIFO_FULL);

		goto err_no_free_tx;
	}

#ifdef DEBUG_CODE
	if (debug_level == 3) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_sys_chan(chan, 3, "TX len %2d: ", skb->len);

	} else if (debug_level >= 4) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_sys_chan(chan, 4,
			"TX (f1=%02x, f2=%02x, z1(f1)=%04x, z2(f2)=%04x)"
			" len %2d: ",
			fifo->f1, fifo->f2, fifo->z1, fifo->z2,
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
	hfc_fifo_next_frame(fifo);

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

	if (chan->connected_st_chan) {
		struct hfc_led *led =
			&card->leds[chan->connected_st_chan->port->id];

		led->alt_color = HFC_LED_OFF;
		led->flashing_freq = HZ / 10;
		led->flashes = 1;
		hfc_led_update(led);
	}

	return VISDN_TX_OK;

err_no_free_tx:
	hfc_card_unlock(card);

	return VISDN_TX_BUSY;
}

static ssize_t hfc_sys_chan_read(
	struct visdn_leg *visdn_leg,
	void *buf, size_t count)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;

	hfc_card_lock(card);

	hfc_fifo_select(&chan->rx_fifo);

	available_octets = hfc_fifo_used_rx(&chan->rx_fifo);

	copied_octets = available_octets < count ? available_octets : count;

	if (available_octets > chan->rx_fifo.stats_max)
		chan->rx_fifo.stats_max = available_octets;

	if (available_octets - copied_octets < chan->rx_fifo.stats_min)
		chan->rx_fifo.stats_min = available_octets - copied_octets;

	chan->rx_fifo.stats_cycles++;

	hfc_fifo_mem_read(&chan->rx_fifo, buf, copied_octets);

	hfc_card_unlock(card);

	return copied_octets;
}

static ssize_t hfc_sys_chan_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_leg->chan);
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;
	int used_octets;

	hfc_card_lock(card);

	hfc_fifo_select(&chan->tx_fifo);

	available_octets = hfc_fifo_free_tx(&chan->tx_fifo);

	copied_octets = available_octets > count ? count : available_octets;

	used_octets = hfc_fifo_used_tx(&chan->tx_fifo);

	chan->tx_fifo.stats_cycles++;
	if (chan->tx_fifo.stats_cycles >= 10) {
		if (chan->tx_fifo.stats_min < HFC_FIFO_JITTBUFF) {
			u8 foo = ((u8 *)buf)[0];
			hfc_fifo_mem_write(&chan->tx_fifo, &foo, 1);
			printk(KERN_DEBUG "Added one sample\n");
		}

		chan->tx_fifo.stats_cycles = 0;
		chan->tx_fifo.stats_min = INT_MAX;
		chan->tx_fifo.stats_max = 0;
	}

	if (used_octets < chan->tx_fifo.stats_min)
		chan->tx_fifo.stats_min = used_octets;

	if (used_octets + copied_octets > chan->tx_fifo.stats_max)
		chan->tx_fifo.stats_max = used_octets + copied_octets;

	hfc_fifo_mem_write(&chan->tx_fifo, buf, copied_octets);

	hfc_card_unlock(card);

	return copied_octets;
}

static void hfc_sys_chan_rx_work(void *data)
{
	struct hfc_sys_chan *chan = data;
	struct hfc_card *card = chan->port->card;
	struct hfc_fifo *fifo = &chan->rx_fifo;
	int frame_size;
	struct sk_buff *skb;
	struct { u8 crc[2], stat; } __attribute((packed)) stat;

	hfc_card_lock(card);

	// FIFO selection has to be done for each frame to clear
	// internal buffer (see specs 4.4.4).
	hfc_fifo_select(fifo);

	if (!hfc_fifo_has_frames(fifo))
		goto no_frames;

	// frame_size includes CRC+CRC+STAT
	frame_size = hfc_fifo_get_frame_size(fifo);

	if (frame_size < 3) {
		hfc_debug_sys_chan(chan, 3,
			"invalid frame received, just %d bytes\n",
			frame_size);

		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_LENGTH);

		goto err_invalid_frame;
	} else if(frame_size == 3) {
		hfc_debug_sys_chan(chan, 3,
			"empty frame received\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_LENGTH);

		goto err_empty_frame;
	}

	skb = visdn_alloc_skb(frame_size - 3);

	if (!skb) {
		hfc_msg_sys_chan(chan, KERN_ERR,
			"cannot allocate skb: frame dropped\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_DROPPED);

		goto err_alloc_skb;
	}

	hfc_fifo_mem_read(fifo, skb_put(skb, frame_size - 3), frame_size - 3);

	hfc_fifo_mem_read(fifo, &stat, sizeof(stat));

#ifdef DEBUG_CODE
	if(debug_level == 3) {
		hfc_debug_sys_chan(chan, 3, "RX len %2d: ", frame_size);
	} else if(debug_level >= 4) {
		hfc_debug_sys_chan(chan, 4,
			"RX (f1=%02x, f2=%02x, z1(f1)=%04x, z2(f1)=%04x)"
			" len %2d: ",
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

		hfc_debug_sys_chan(chan, 3, "Frame abort detected\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_FR_ABORT);

		goto err_frame_abort;

	} else if (stat.stat != 0x00) {
		// CRC not ok, frame broken, skipping
		hfc_debug_sys_chan(chan, 2, "Received frame with wrong CRC\n");

		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
				VISDN_RX_ERROR_CRC);

		goto err_crc_error;
	}

	hfc_fifo_next_frame(fifo);

	visdn_leg_frame_xmit(
		&chan->visdn_chan.leg_b,
		skb);

	if (chan->connected_st_chan) {
		struct hfc_led *led =
			&card->leds[chan->connected_st_chan->port->id];

		led->alt_color = HFC_LED_OFF;
		led->flashing_freq = HZ / 10;
		led->flashes = 1;
		hfc_led_update(led);
	}

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

	hfc_fifo_refresh_fz_cache(fifo);

	if (hfc_fifo_has_frames(fifo))
		schedule_work(&chan->rx_work);

	hfc_card_unlock(card);
}

struct visdn_chan_ops hfc_sys_chan_ops =
{
	.owner			= THIS_MODULE,

	.release		= hfc_sys_chan_release,
	.open			= hfc_sys_chan_open,
	.close			= hfc_sys_chan_close,
};

struct visdn_leg_ops hfc_sys_chan_leg_a_ops =
{
	.owner		= THIS_MODULE,

	.connect	= hfc_sys_chan_leg_a_connect,
	.disconnect	= hfc_sys_chan_leg_a_disconnect,
};

struct visdn_leg_ops hfc_sys_chan_leg_b_ops = {
	.owner		= THIS_MODULE,

	.connect	= hfc_sys_chan_leg_b_connect,
	.disconnect	= hfc_sys_chan_leg_b_disconnect,

	.frame_xmit	= hfc_sys_chan_frame_xmit,

	.read		= hfc_sys_chan_read,
	.write		= hfc_sys_chan_write,
};

struct visdn_chan_class hfc_sys_chan_class =
{
	.name	= "sys"
};

void hfc_sys_chan_init(
	struct hfc_sys_chan *chan,
	struct hfc_sys_port *port,
	const char *name,
	int id)
{
	chan->port = port;
	chan->id = id;

	hfc_fifo_init(&chan->rx_fifo, chan, id, RX);
	hfc_fifo_init(&chan->tx_fifo, chan, id, TX);

	visdn_chan_init(&chan->visdn_chan);

	chan->visdn_chan.ops = &hfc_sys_chan_ops;
	chan->visdn_chan.chan_class = &hfc_sys_chan_class;
	chan->visdn_chan.port = &port->visdn_port;

	chan->visdn_chan.leg_a.cxc = &port->card->cxc.visdn_cxc;
	chan->visdn_chan.leg_a.ops = &hfc_sys_chan_leg_a_ops;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.cxc = &vsc_softcxc.cxc;
	chan->visdn_chan.leg_b.ops = &hfc_sys_chan_leg_b_ops;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE |
						VISDN_LEG_FRAMING_HDLC;
	chan->visdn_chan.leg_b.mtu = 0;

	strncpy(chan->visdn_chan.name, name, sizeof(chan->visdn_chan.name));
	chan->visdn_chan.driver_data = chan;

	INIT_WORK(&chan->rx_work,
		hfc_sys_chan_rx_work,
		chan);
}

int hfc_sys_chan_register(
	struct hfc_sys_chan *chan)
{
	int err;

	chan->visdn_chan.leg_b.mtu = chan->tx_fifo.size;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

	{
	struct visdn_chan_attribute **attr = hfc_sys_attributes;

	while(*attr) {
		visdn_chan_create_file(
			&chan->visdn_chan,
			*attr);

		attr++;
	}
	}

	return 0;

err_chan_register:
	return err;
}

void hfc_sys_chan_unregister(
	struct hfc_sys_chan *chan)
{
	struct visdn_chan_attribute **attr = hfc_sys_attributes;

	while(*attr) {
		visdn_chan_remove_file(
			&chan->visdn_chan,
			*attr);

		attr++;
	}

	visdn_chan_unregister(&chan->visdn_chan);
}

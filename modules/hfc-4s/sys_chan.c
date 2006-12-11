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

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/dynattr.h>
#include <linux/kstreamer/softswitch.h>

#include <linux/visdn/visdn.h>

#include "card.h"
#include "switch.h"
#include "sys_port.h"
#include "sys_chan.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "st_port_inline.h"

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
			(chan)->ks_duplex.kobj.name,			\
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
		(chan)->ks_duplex.kobj.name,				\
		## arg)

#if 0
/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_rx_fifo_size(
	struct visdn_chan *visdn_chan,
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);
	int fifo_used;

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->rx_fifo);
	fifo_used = hfc_fifo_used(&chan->rx_fifo);
	hfc_card_unlock(chan->port->card);

	return snprintf(buf, PAGE_SIZE, "%d\n", fifo_used);
}

static ssize_t hfc_store_rx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct hfc_sys_chan *chan = to_sys_chan(visdn_chan);

	int fifo_used;

	hfc_card_lock(chan->port->card);
	hfc_fifo_select(&chan->tx_fifo);
	fifo_used = hfc_fifo_used(&chan->tx_fifo);
	hfc_card_unlock(chan->port->card);

	return snprintf(buf, PAGE_SIZE, "%d\n", fifo_used);
}

static ssize_t hfc_store_tx_fifo_used(
	struct visdn_chan *visdn_chan,
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
	struct ks_chan_attribute *attr,
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
#endif

static struct ks_chan_attribute *hfc_sys_chan_rx_attributes[] =
{
/*
	&ks_chan_attr_rx_fifo_size,
	&ks_chan_attr_rx_fifo_used,
	&ks_chan_attr_rx_fifo_min,
	&ks_chan_attr_rx_fifo_max,

	&ks_chan_attr_tx_fifo_size,
	&ks_chan_attr_tx_fifo_used,
	&ks_chan_attr_tx_fifo_min,
	&ks_chan_attr_tx_fifo_max,

	&ks_chan_attr_rx_subchannel_bit_start,
	&ks_chan_attr_tx_subchannel_bit_start,
	&ks_chan_attr_rx_subchannel_bit_count,
	&ks_chan_attr_tx_subchannel_bit_count,
*/
	NULL
};

static struct ks_chan_attribute *hfc_sys_chan_tx_attributes[] =
{
/*
	&ks_chan_attr_rx_fifo_size,
	&ks_chan_attr_rx_fifo_used,
	&ks_chan_attr_rx_fifo_min,
	&ks_chan_attr_rx_fifo_max,

	&ks_chan_attr_tx_fifo_size,
	&ks_chan_attr_tx_fifo_used,
	&ks_chan_attr_tx_fifo_min,
	&ks_chan_attr_tx_fifo_max,

	&ks_chan_attr_rx_subchannel_bit_start,
	&ks_chan_attr_tx_subchannel_bit_start,
	&ks_chan_attr_rx_subchannel_bit_count,
	&ks_chan_attr_tx_subchannel_bit_count,
*/
	NULL
};

/*------------------------------- Duplex ------------------------------------*/

static void hfc_sys_chan_duplex_release(struct ks_duplex *duplex)
{
	struct hfc_sys_chan *chan =
		container_of(duplex, struct hfc_sys_chan, ks_duplex);

	printk(KERN_DEBUG "hfc_sys_chan_duplex_release()\n");

	hfc_sys_port_put(chan->port);
}

/*------------------------------- RX Link -----------------------------------*/

static void hfc_sys_chan_rx_chan_release(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;

	hfc_msg_sys_chan(chan, KERN_DEBUG, "hfc_sys_chan_rx_chan_release()\n");

	hfc_sys_chan_put(chan);
}

static int hfc_sys_chan_rx_chan_connect(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;

	hfc_debug_sys_chan(chan, 2, "RX connected\n");

	chan_rx->fifo.bit_reversed = FALSE;
	chan_rx->fifo.framer_enabled = FALSE;

	return 0;
}

static void hfc_sys_chan_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;

	chan_rx->fifo.bit_reversed = FALSE;
	chan_rx->fifo.framer_enabled = FALSE;

	hfc_debug_sys_chan(chan, 2, "RX disconnected\n");
}

static int hfc_sys_chan_rx_chan_open(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;
	struct ks_chan *prev_chan;
	int err;

	hfc_card_lock(card);

	chan_rx->fifo.subchannel_bit_start = 0;
	chan_rx->fifo.subchannel_bit_count = 8;

	prev_chan = ks_pipeline_prev(ks_chan);
	if (prev_chan && prev_chan->ops == &hfc_st_chan_rx_chan_ops) {
			struct hfc_st_chan_rx *st_chan_rx =
				container_of(prev_chan, struct hfc_st_chan_rx,
								ks_chan);

		chan_rx->fifo.subchannel_bit_start =
			st_chan_rx->chan->subchannel_bit_start;

		chan_rx->fifo.subchannel_bit_count =
			st_chan_rx->chan->subchannel_bit_count;
	} else if (prev_chan && prev_chan->ops == &hfc_pcm_chan_rx_chan_ops) {
//			struct hfc_pcm_chan_rx *pcm_chan_rx =
//				container_of(prev_chan, struct hfc_pcm_chan_rx,
//								ks_chan);

		chan_rx->fifo.subchannel_bit_start = 0;
		chan_rx->fifo.subchannel_bit_count = 8;
	}

	hfc_sys_port_update_fsm(chan->port);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "RX channel opened\n");

	return 0;

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "RX channel opening failed: %d\n", err);

	return err;
}

static void hfc_sys_chan_rx_chan_close(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);
	hfc_sys_port_update_fsm(chan->port);
	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "RX channel closed.\n");
}

static int hfc_sys_chan_rx_chan_start(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	chan_rx->fifo.enabled = TRUE;

	hfc_fifo_select(&chan_rx->fifo);
	hfc_fifo_reset(&chan_rx->fifo);
	hfc_fifo_configure(&chan_rx->fifo);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "RX channel started\n");

	return 0;
}

static void hfc_sys_chan_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	chan_rx->fifo.enabled = FALSE;

	hfc_fifo_select(&chan_rx->fifo);
	hfc_fifo_reset(&chan_rx->fifo);
	hfc_fifo_configure(&chan_rx->fifo);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "RX channel stopped\n");
}

static void hfc_sys_chan_rx_chan_stimulus(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;

	struct ks_streamframe *sf;

	sf = ks_sf_alloc();
	if (!sf)
		return;

	hfc_card_lock(card);

	hfc_fifo_select(&chan_rx->fifo);

	available_octets = hfc_fifo_used(&chan_rx->fifo);

	copied_octets = available_octets < sf->size ?
				available_octets : sf->size;

	if (available_octets > chan_rx->fifo.stats_max)
		chan_rx->fifo.stats_max = available_octets;

	if (available_octets - copied_octets < chan_rx->fifo.stats_min)
		chan_rx->fifo.stats_min = available_octets - copied_octets;

	chan_rx->fifo.stats_cycles++;

	hfc_fifo_mem_read(&chan_rx->fifo, sf->data, copied_octets);
	sf->len = copied_octets;

	hfc_card_unlock(card);

	vss_chan_push_raw(ks_chan, sf);

	ks_sf_put(sf);
}

static int hfc_sys_chan_rx_chan_get_attr_count(struct ks_chan *chan)
{
	return 2;
}

static int hfc_sys_chan_rx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);

	switch(index) {
	case 0: {
		struct ks_hdlc_deframer_descr *descr = buf;

		if (*len < sizeof(struct ks_hdlc_deframer_descr))
			return -ENOSPC;

		*type = hfc_hdlc_deframer_class->id;
		*len = sizeof(struct ks_hdlc_deframer_descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = chan_rx->fifo.framer_enabled ? 1 : 0;
	}
	break;

	case 1: {
		struct ks_octet_reverser_descr *descr = buf;

		if (*len < sizeof(struct ks_octet_reverser_descr))
			return -ENOSPC;

		*type = hfc_octet_reverser_class->id;
		*len = sizeof(struct ks_octet_reverser_descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = chan_rx->fifo.bit_reversed ? 1 : 0;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int hfc_sys_chan_rx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);

printk(KERN_DEBUG "BBBBBBBBBB %d %d %d\n", type, hfc_hdlc_deframer_class->id, hfc_octet_reverser_class->id);

	if (type == hfc_hdlc_deframer_class->id) {
		struct ks_hdlc_deframer_descr *descr = buf;

		if (len < sizeof(struct ks_hdlc_deframer_descr))
			return -EINVAL;

		chan_rx->fifo.framer_enabled = descr->enabled;

	} else if (type == hfc_octet_reverser_class->id) {
		struct ks_octet_reverser_descr *descr = buf;

printk(KERN_DEBUG "CCCCCCCCCC %d %d\n", descr->hardware, descr->enabled);

		if (len < sizeof(struct ks_octet_reverser_descr))
			return -EINVAL;

		chan_rx->fifo.bit_reversed = descr->enabled;

	} else
		return -ENOENT;

	return 0;
}

static struct ks_chan_ops hfc_sys_chan_rx_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_sys_chan_rx_chan_release,
	.connect	= hfc_sys_chan_rx_chan_connect,
	.disconnect	= hfc_sys_chan_rx_chan_disconnect,
	.open		= hfc_sys_chan_rx_chan_open,
	.close		= hfc_sys_chan_rx_chan_close,
	.start		= hfc_sys_chan_rx_chan_start,
	.stop		= hfc_sys_chan_rx_chan_stop,
	.stimulus	= hfc_sys_chan_rx_chan_stimulus,
	.get_attr_count	= hfc_sys_chan_rx_chan_get_attr_count,
	.get_attr	= hfc_sys_chan_rx_chan_get_attr,
	.set_attr	= hfc_sys_chan_rx_chan_set_attr,
};

/*------------------------------- TX Link -----------------------------------*/

static void hfc_sys_chan_tx_chan_release(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_rx *chan_rx = to_sys_chan_rx(ks_chan);
	struct hfc_sys_chan *chan = chan_rx->chan;

	hfc_msg_sys_chan(chan, KERN_DEBUG, "hfc_sys_chan_tx_chan_release()\n");

	hfc_sys_chan_put(chan);
}

static int hfc_sys_chan_tx_chan_connect(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;

	hfc_debug_sys_chan(chan, 2, "TX connected\n");

	chan_tx->fifo.bit_reversed = FALSE;
	chan_tx->fifo.framer_enabled = FALSE;

	return 0;
}

static void hfc_sys_chan_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;

	chan_tx->fifo.bit_reversed = FALSE;
	chan_tx->fifo.framer_enabled = FALSE;

	hfc_debug_sys_chan(chan, 2, "TX disconnected\n");
}

static int hfc_sys_chan_tx_chan_open(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;
	struct ks_chan *next_chan;
	int err;

	hfc_card_lock(card);

	chan_tx->fifo.subchannel_bit_start = 0;
	chan_tx->fifo.subchannel_bit_count = 8;

	next_chan = ks_pipeline_next(ks_chan);
	if (next_chan && next_chan->ops == &hfc_st_chan_tx_chan_ops) {
			struct hfc_st_chan_tx *st_chan_tx =
				container_of(next_chan, struct hfc_st_chan_tx,
								ks_chan);

		chan_tx->fifo.subchannel_bit_start =
			st_chan_tx->chan->subchannel_bit_start;

		chan_tx->fifo.subchannel_bit_count =
			st_chan_tx->chan->subchannel_bit_count;
	} else if (next_chan && next_chan->ops == &hfc_pcm_chan_rx_chan_ops) {
//			struct hfc_pcm_chan_rx *pcm_chan_rx =
//				container_of(next_chan, struct hfc_pcm_chan_rx,
//								ks_chan);
	}

	hfc_sys_port_update_fsm(chan->port);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "TX channel opened\n");

	return 0;

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "TX channel opening failed: %d\n", err);

	return err;
}

static void hfc_sys_chan_tx_chan_close(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);
	hfc_sys_port_update_fsm(chan->port);
	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "TX channel closed.\n");
}

static int hfc_sys_chan_tx_chan_start(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	chan_tx->fifo.enabled = TRUE;

	/* TX FIFO */
	hfc_fifo_select(&chan_tx->fifo);
	hfc_fifo_reset(&chan_tx->fifo);
	hfc_fifo_configure(&chan_tx->fifo);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "TX channel started\n");

	return 0;
}

static void hfc_sys_chan_tx_chan_stop(struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;

	hfc_card_lock(card);

	chan_tx->fifo.enabled = FALSE;

	hfc_fifo_select(&chan_tx->fifo);
	hfc_fifo_reset(&chan_tx->fifo);
	hfc_fifo_configure(&chan_tx->fifo);

	hfc_card_unlock(card);

	hfc_debug_sys_chan(chan, 1, "TX channel stopped\n");
}

static int hfc_sys_chan_tx_chan_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_fifo *fifo = &chan_tx->fifo;
	struct hfc_card *card = chan->port->card;

	/* Remove CRC, the hardware is going to generate one for us */
	int frame_len = skb->len - 2;

	hfc_card_lock(card);
	hfc_fifo_select(fifo);
	/*
	 * hfc_fifo_select() updates F/Z cache, so,
	 * size calculations are allowed
	 */

	if (hfc_fifo_free_frames(fifo) <= 1) {
		hfc_debug_sys_chan(chan, 3,
			"TX FIFO frames full, throttling\n");

		//visdn_leg_stop_queue(&chan->visdn_chan.leg_b);
	}

	if (hfc_fifo_free_tx(fifo) < frame_len) {
		hfc_debug_sys_chan(chan, 3,
			"TX FIFO full (%d < %d), throttling\n",
			hfc_fifo_free_tx(fifo), frame_len);

		//visdn_leg_stop_queue(&chan->visdn_chan.leg_b);

		//visdn_leg_tx_error(&chan->visdn_chan.leg_b,
		//		VISDN_TX_ERROR_FIFO_FULL);

		goto err_no_free_tx;
	}

#ifdef DEBUG_CODE
	if (debug_level == 3) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_sys_chan(chan, 3, "TX len %2d: ", frame_len);

	} else if (debug_level >= 4) {
		hfc_fifo_refresh_fz_cache(fifo);
		hfc_debug_sys_chan(chan, 4,
			"TX (f1=%02x, f2=%02x, z1(f1)=%04x, z2(f2)=%04x)"
			" len %2d: ",
			fifo->f1, fifo->f2, fifo->z1, fifo->z2,
			frame_len);
	}

	if (debug_level >= 3) {
		int i;
		for (i=0; i<frame_len; i++)
			printk("%02x",((u8 *)skb->data)[i]);

		printk("\n");
	}
#endif

	hfc_fifo_mem_write(fifo, skb->data, frame_len);
	hfc_fifo_next_frame(fifo);

	hfc_card_unlock(card);

	visdn_kfree_skb(skb);

/*	if (chan->connected_st_chan) {
		struct hfc_led *led =
			chan->connected_st_chan->port->led;

		if (led) {
			led->alt_color = HFC_LED_OFF;
			led->flashing_freq = HZ / 10;
			led->flashes = 1;
			hfc_led_update(led);
		}
	}*/

	return KS_TX_OK;

err_no_free_tx:
	hfc_card_unlock(card);

	return KS_TX_BUSY;
}

static int hfc_sys_chan_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_fifo *fifo = &chan_tx->fifo;
	struct hfc_card *card = chan->port->card;
	int copied_octets;
	int available_octets;

	hfc_card_lock(card);

	hfc_fifo_select(fifo);

	available_octets = hfc_fifo_free_tx(fifo);
	copied_octets = min(available_octets, (int)sf->len);

	hfc_fifo_mem_write(fifo, sf->data, copied_octets);

	/* FIFO reselection is mandatory, otherwise Z1 is not updated */
	hfc_fifo_select(fifo);

	hfc_card_unlock(card);

	return copied_octets;
}

static int hfc_sys_chan_tx_chan_get_pressure(
	struct ks_chan *ks_chan)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);
	struct hfc_sys_chan *chan = chan_tx->chan;
	struct hfc_card *card = chan->port->card;
	int pressure;

	hfc_card_lock(card);
	hfc_fifo_select(&chan_tx->fifo);
	pressure = hfc_fifo_used(&chan_tx->fifo);
	hfc_card_unlock(card);

	return pressure;
}

static int hfc_sys_chan_tx_chan_get_attr_count(struct ks_chan *chan)
{
	return 2;
}

static int hfc_sys_chan_tx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);

	switch(index) {
	case 0: {
		struct ks_hdlc_framer_descr *descr = buf;

		if (*len < sizeof(struct ks_hdlc_framer_descr))
			return -ENOSPC;

		*type = hfc_hdlc_framer_class->id;
		*len = sizeof(struct ks_hdlc_framer_descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = chan_tx->fifo.framer_enabled ? 1 : 0;
	}
	break;

	case 1: {
		struct ks_octet_reverser_descr *descr = buf;

		if (*len < sizeof(struct ks_octet_reverser_descr))
			return -ENOSPC;

		*type = hfc_octet_reverser_class->id;
		*len = sizeof(struct ks_octet_reverser_descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = chan_tx->fifo.bit_reversed ? 1 : 0;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int hfc_sys_chan_tx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct hfc_sys_chan_tx *chan_tx = to_sys_chan_tx(ks_chan);

	if (type == hfc_hdlc_framer_class->id) {
		struct ks_hdlc_framer_descr *descr = buf;

		if (len < sizeof(struct ks_hdlc_framer_descr))
			return -EINVAL;

		chan_tx->fifo.framer_enabled = descr->enabled;

	} else if (type == hfc_octet_reverser_class->id) {

		struct ks_octet_reverser_descr *descr = buf;

		if (len < sizeof(struct ks_octet_reverser_descr))
			return -EINVAL;

		chan_tx->fifo.bit_reversed = descr->enabled;
	} else
		return -ENOENT;

	return 0;
}

struct ks_chan_ops hfc_sys_chan_tx_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= hfc_sys_chan_tx_chan_release,
	.connect	= hfc_sys_chan_tx_chan_connect,
	.disconnect	= hfc_sys_chan_tx_chan_disconnect,
	.open		= hfc_sys_chan_tx_chan_open,
	.close		= hfc_sys_chan_tx_chan_close,
	.start		= hfc_sys_chan_tx_chan_start,
	.stop		= hfc_sys_chan_tx_chan_stop,
	.get_attr_count	= hfc_sys_chan_tx_chan_get_attr_count,
	.get_attr	= hfc_sys_chan_tx_chan_get_attr,
	.set_attr	= hfc_sys_chan_tx_chan_set_attr,
};

struct vss_chan_ops hfc_sys_chan_tx_node_ops =
{
	.push_frame	= hfc_sys_chan_tx_chan_push_frame,
	.push_raw	= hfc_sys_chan_tx_chan_push_raw,
	.get_pressure	= hfc_sys_chan_tx_chan_get_pressure,
};

/*---------------------------------------------------------------------------*/

struct ks_duplex_ops hfc_sys_chan_duplex_ops =
{
	.owner			= THIS_MODULE,

	.release		= hfc_sys_chan_duplex_release,

/*	.open			= hfc_sys_chan_open,
	.close			= hfc_sys_chan_close,
	.start			= hfc_sys_chan_start,
	.stop			= hfc_sys_chan_stop,*/
};

/*---------------------------------------------------------------------------*/

static void hfc_sys_chan_rx_tasklet(unsigned long data)
{
	struct hfc_sys_chan_rx *chan_rx = (struct hfc_sys_chan_rx *)data;
	struct hfc_sys_chan *chan = chan_rx->chan;
	struct hfc_card *card = chan->port->card;
	struct hfc_fifo *fifo = &chan->rx.fifo;
	int frame_size;
	struct sk_buff *skb;
	u8 stat;

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

//		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
//				VISDN_RX_ERROR_LENGTH);

		goto err_invalid_frame;

	} else if(frame_size == 3) {
		hfc_debug_sys_chan(chan, 3,
			"empty frame received\n");

//		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
//				VISDN_RX_ERROR_LENGTH);

		goto err_empty_frame;
	}

	skb = visdn_alloc_skb(frame_size - 1);

	if (!skb) {
		hfc_msg_sys_chan(chan, KERN_ERR,
			"cannot allocate skb: frame dropped\n");

//		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
//				VISDN_RX_ERROR_DROPPED);

		goto err_alloc_skb;
	}

	hfc_fifo_mem_read(fifo, skb_put(skb, frame_size - 1), frame_size - 1);

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
		for (i=0; i<frame_size - 1; i++)
			printk("%02x", ((u8 *)skb->data)[i]);

		printk(" %02x\n", stat);
	}
#endif

	if (stat == 0xff) {
		// Frame abort detected

		hfc_debug_sys_chan(chan, 3, "Frame abort detected\n");

//		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
//				VISDN_RX_ERROR_FR_ABORT);

		goto err_frame_abort;

	} else if (stat != 0x00) {
		// CRC not ok, frame broken, skipping
		hfc_debug_sys_chan(chan, 2, "Received frame with wrong CRC\n");

//		visdn_leg_rx_error(&chan->visdn_chan.leg_b,
//				VISDN_RX_ERROR_CRC);

		goto err_crc_error;
	}

	hfc_fifo_next_frame(fifo);

	vss_chan_push_frame(&chan_rx->ks_chan, skb);

#if 0
	if (chan->connected_st_chan) {
		struct hfc_led *led =
			chan->connected_st_chan->port->led;

		if (led) {
			led->alt_color = HFC_LED_OFF;
			led->flashing_freq = HZ / 10;
			led->flashes = 1;
			hfc_led_update(led);
		}
	}
#endif

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
		tasklet_schedule(&chan_rx->tasklet);

	hfc_card_unlock(card);
}

static void hfc_sys_chan_rx_init(
	struct hfc_sys_chan_rx *chan_rx,
	struct hfc_sys_chan *chan,
	int fifo_hwid)
{
	chan_rx->chan = chan;

	ks_chan_init(&chan_rx->ks_chan,
			&hfc_sys_chan_rx_chan_ops, "rx",
			&chan->ks_duplex,
			&chan->ks_duplex.kobj,
			&chan->port->card->hfcswitch.ks_node,
			&vss_softswitch.ks_node);

	hfc_fifo_init(&chan_rx->fifo, chan->port->card, fifo_hwid, RX);

	chan_rx->ks_chan.mtu = -1;

	tasklet_init(&chan_rx->tasklet,
		hfc_sys_chan_rx_tasklet,
		(unsigned long)chan_rx);
}

static void hfc_sys_chan_tx_init(
	struct hfc_sys_chan_tx *chan_tx,
	struct hfc_sys_chan *chan,
	int fifo_hwid)
{
	chan_tx->chan = chan;

	ks_chan_init(&chan_tx->ks_chan,
			&hfc_sys_chan_tx_chan_ops, "tx",
			&chan->ks_duplex,
			&chan->ks_duplex.kobj,
			&vss_softswitch.ks_node,
			&chan->port->card->hfcswitch.ks_node);

	chan_tx->ks_chan.from_ops = &hfc_sys_chan_tx_node_ops;

	chan_tx->ks_chan.mtu = -1;

	hfc_fifo_init(&chan_tx->fifo, chan->port->card, fifo_hwid, TX);
}

void hfc_sys_chan_init(
	struct hfc_sys_chan *chan,
	struct hfc_sys_port *port,
	const char *name,
	int fifo_hwid)
{
	chan->port = port;
//	chan->id = id;

	ks_duplex_init(&chan->ks_duplex,
			&hfc_sys_chan_duplex_ops,
			name,
			&port->visdn_port.kobj);

	hfc_sys_chan_rx_init(&chan->rx, chan, fifo_hwid);
	hfc_sys_chan_tx_init(&chan->tx, chan, fifo_hwid);
}

static int hfc_sys_chan_rx_register(struct hfc_sys_chan_rx *chan_rx)
{
	int err;

	err = ks_chan_register(&chan_rx->ks_chan);
	if (err < 0)
		goto err_chan_register;

	{
	struct ks_chan_attribute **attr = hfc_sys_chan_rx_attributes;

	while(*attr) {
		ks_chan_create_file(
			&chan_rx->ks_chan,
			*attr);

		attr++;
	}
	}

	return 0;

	ks_chan_unregister(&chan_rx->ks_chan);
err_chan_register:

	return err;
}

static void hfc_sys_chan_rx_unregister(struct hfc_sys_chan_rx *chan_rx)
{
	struct ks_chan_attribute **attr = hfc_sys_chan_rx_attributes;

	while(*attr) {
		ks_chan_remove_file(
			&chan_rx->ks_chan,
			*attr);

		attr++;
	}

	ks_chan_unregister(&chan_rx->ks_chan);
}

static int hfc_sys_chan_tx_register(struct hfc_sys_chan_tx *chan_tx)
{
	int err;

	chan_tx->ks_chan.mtu = chan_tx->fifo.size;

	err = ks_chan_register(&chan_tx->ks_chan);
	if (err < 0)
		goto err_chan_register;

	{
	struct ks_chan_attribute **attr = hfc_sys_chan_tx_attributes;

	while(*attr) {
		ks_chan_create_file(
			&chan_tx->ks_chan,
			*attr);

		attr++;
	}
	}

	return 0;

	ks_chan_unregister(&chan_tx->ks_chan);
err_chan_register:

	return err;
}

static void hfc_sys_chan_tx_unregister(struct hfc_sys_chan_tx *chan_tx)
{
	struct ks_chan_attribute **attr = hfc_sys_chan_tx_attributes;

	while(*attr) {
		ks_chan_remove_file(
			&chan_tx->ks_chan,
			*attr);

		attr++;
	}

	ks_chan_unregister(&chan_tx->ks_chan);
}

int hfc_sys_chan_register(
	struct hfc_sys_chan *chan)
{
	int err;

	err = ks_duplex_register(&chan->ks_duplex);
	if (err < 0)
		goto err_duplex_register;

	hfc_sys_chan_get(chan);
	err = hfc_sys_chan_rx_register(&chan->rx);
	if (err < 0)
		goto err_sys_chan_rx_register;

	hfc_sys_chan_get(chan);
	err = hfc_sys_chan_tx_register(&chan->tx);
	if (err < 0)
		goto err_sys_chan_tx_register;

	return 0;

	ks_duplex_unregister(&chan->ks_duplex);
err_duplex_register:
	hfc_sys_chan_tx_unregister(&chan->tx);
err_sys_chan_tx_register:
	hfc_sys_chan_rx_unregister(&chan->rx);
err_sys_chan_rx_register:

	return err;
}

void hfc_sys_chan_unregister(
	struct hfc_sys_chan *chan)
{
	hfc_sys_chan_tx_unregister(&chan->tx);
	hfc_sys_chan_rx_unregister(&chan->rx);

	ks_duplex_unregister(&chan->ks_duplex);
}

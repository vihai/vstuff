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

#include "card.h"
#include "pcm_port.h"
#include "pcm_chan.h"

#ifdef DEBUG_CODE
#define hfc_debug_pcm_chan(chan, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX			\
			"%s-%s:"					\
			"pcm:"						\
			"chan[%s] "					\
			format,						\
			(chan)->port->card->pci_dev->dev.bus->name,	\
			(chan)->port->card->pci_dev->dev.bus_id,	\
			kobject_name(&(chan)->ks_node.kobj),		\
			## arg)

#else
#define hfc_debug_pcm_chan(chan, dbglevel, format, arg...) do {} while (0)
#define hfc_debug_schan(schan, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_pcm_chan(chan, level, format, arg...)			\
	printk(level hfc_DRIVER_PREFIX					\
		"%s-%s:"						\
		"pcm:"							\
		"chan[%s] "						\
		format,							\
		(chan)->port->card->pci_dev->dev.bus->name,		\
		(chan)->port->card->pci_dev->dev.bus_id,		\
		kobject_name(&(chan)->visdn_chan.kobj),			\
		## arg)

static void hfc_pcm_chan_rx_link_release(struct ks_link *ks_link)
{
	printk(KERN_DEBUG "hfc_pcm_chan_rx_link_release()\n");

	// FIXME
}

static int hfc_pcm_chan_rx_link_connect(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX connected\n");

	return 0;
}

static void hfc_pcm_chan_rx_link_disconnect(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX disconnected\n");
}

static int hfc_pcm_chan_rx_link_open(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX opened\n");

	return 0;
}

static void hfc_pcm_chan_rx_link_close(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX closed\n");
}

static int hfc_pcm_chan_rx_link_start(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX started\n");

	return 0;
}

static void hfc_pcm_chan_rx_link_stop(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_rx *chan_rx =
		container_of(ks_link, struct hfc_pcm_chan_rx, ks_link);
	struct hfc_pcm_chan *chan = chan_rx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX stopped\n");
}

struct ks_link_ops hfc_pcm_chan_rx_link_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_pcm_chan_rx_link_release,
	.connect		= hfc_pcm_chan_rx_link_connect,
	.disconnect		= hfc_pcm_chan_rx_link_disconnect,
	.open			= hfc_pcm_chan_rx_link_open,
	.close			= hfc_pcm_chan_rx_link_close,
	.start			= hfc_pcm_chan_rx_link_start,
	.stop			= hfc_pcm_chan_rx_link_stop,
};

/*----------------------------------------------------------------------------*/

static void hfc_pcm_chan_tx_link_release(struct ks_link *ks_link)
{
	printk(KERN_DEBUG "hfc_pcm_chan_tx_link_release()\n");

	// FIXME
}

static int hfc_pcm_chan_tx_link_connect(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX connected\n");

	return 0;
}

static void hfc_pcm_chan_tx_link_disconnect(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX disconnected\n");
}

static int hfc_pcm_chan_tx_link_open(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX opened\n");

	return 0;
}

static void hfc_pcm_chan_tx_link_close(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX closed\n");
}

static int hfc_pcm_chan_tx_link_start(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX started\n");

	return 0;
}

static void hfc_pcm_chan_tx_link_stop(struct ks_link *ks_link)
{
	struct hfc_pcm_chan_tx *chan_tx =
		container_of(ks_link, struct hfc_pcm_chan_tx, ks_link);
	struct hfc_pcm_chan *chan = chan_tx->chan;

	hfc_debug_pcm_chan(chan, 2, "RX stopped\n");
}

struct ks_link_ops hfc_pcm_chan_tx_link_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_pcm_chan_tx_link_release,
	.connect		= hfc_pcm_chan_tx_link_connect,
	.disconnect		= hfc_pcm_chan_tx_link_disconnect,
	.open			= hfc_pcm_chan_tx_link_open,
	.close			= hfc_pcm_chan_tx_link_close,
	.start			= hfc_pcm_chan_tx_link_start,
	.stop			= hfc_pcm_chan_tx_link_stop,
};

/*----------------------------------------------------------------------------*/

static void hfc_pcm_chan_node_release(struct ks_node *ks_node)
{
	struct hfc_pcm_chan *chan =
		container_of(ks_node, struct hfc_pcm_chan, ks_node);

	printk(KERN_DEBUG "hfc_pcm_chan_node_release()\n");

	kfree(chan);
}

static struct ks_node_ops hfc_pcm_chan_node_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_pcm_chan_node_release,
};

/*----------------------------------------------------------------------------*/

void hfc_pcm_chan_rx_init(
	struct hfc_pcm_chan_rx *chan_rx,
	struct hfc_pcm_chan *chan)
{
	chan_rx->chan = chan;

	ks_link_init(&chan_rx->ks_link,
			&hfc_pcm_chan_rx_link_ops, "rx",
			NULL,
			&chan->ks_node.kobj,
			&chan->ks_node,
			&chan->port->card->hfcswitch.ks_node);

/*	chan_rx->ks_link.framed_mtu = -1;
	chan_rx->ks_link.framing_avail = VISDN_LINK_FRAMING_ANY;*/
}

void hfc_pcm_chan_tx_init(
	struct hfc_pcm_chan_tx *chan_tx,
	struct hfc_pcm_chan *chan)
{
	chan_tx->chan = chan;

	ks_link_init(&chan_tx->ks_link,
			&hfc_pcm_chan_tx_link_ops, "tx",
			NULL,
			&chan->ks_node.kobj,
			&chan->port->card->hfcswitch.ks_node,
			&chan->ks_node);

/*	chan_tx->ks_link.framed_mtu = -1;
	chan_tx->ks_link.framing_avail = VISDN_LINK_FRAMING_ANY;*/
}

static int hfc_pcm_chan_rx_register(struct hfc_pcm_chan_rx *chan)
{
	int err;

	err = ks_link_register(&chan->ks_link);
	if (err < 0)
		goto err_link_register;

	return 0;

	ks_link_unregister(&chan->ks_link);
err_link_register:

	return err;
}

static void hfc_pcm_chan_rx_unregister(struct hfc_pcm_chan_rx *chan)
{
	ks_link_unregister(&chan->ks_link);
}

static int hfc_pcm_chan_tx_register(struct hfc_pcm_chan_tx *chan)
{
	int err;

	err = ks_link_register(&chan->ks_link);
	if (err < 0)
		goto err_link_register;

	return 0;

	ks_link_unregister(&chan->ks_link);
err_link_register:

	return err;
}

static void hfc_pcm_chan_tx_unregister(struct hfc_pcm_chan_tx *chan)
{
	ks_link_unregister(&chan->ks_link);
}

struct hfc_pcm_chan *hfc_pcm_chan_alloc(int flags)
{
	return kmalloc(sizeof(struct hfc_pcm_chan), flags);
}

void hfc_pcm_chan_init(
	struct hfc_pcm_chan *chan,
	struct hfc_pcm_port *port,
	const char *name,
	int timeslot)
{
	chan->port = port;
	chan->timeslot= timeslot;

	ks_node_init(&chan->ks_node,
			&hfc_pcm_chan_node_ops, name,
			&port->visdn_port.kobj);

	hfc_pcm_chan_rx_init(&chan->rx, chan);
	hfc_pcm_chan_tx_init(&chan->tx, chan);
}

int hfc_pcm_chan_register(struct hfc_pcm_chan *chan)
{
	int err;

	err = ks_node_register(&chan->ks_node);
	if (err < 0)
		goto err_node_register;

	err = hfc_pcm_chan_rx_register(&chan->rx);
	if (err < 0)
		goto err_pcm_chan_rx_register;

	err = hfc_pcm_chan_tx_register(&chan->tx);
	if (err < 0)
		goto err_pcm_chan_tx_register;

	return 0;

	hfc_pcm_chan_tx_unregister(&chan->tx);
err_pcm_chan_tx_register:
	hfc_pcm_chan_rx_unregister(&chan->rx);
err_pcm_chan_rx_register:
	ks_node_unregister(&chan->ks_node);
err_node_register:

	return err;
}

void hfc_pcm_chan_unregister(struct hfc_pcm_chan *chan)
{
	hfc_pcm_chan_tx_unregister(&chan->tx);
	hfc_pcm_chan_rx_unregister(&chan->rx);

	ks_node_unregister(&chan->ks_node);
}

/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
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
#include "card_inline.h"
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
			(chan)->visdn_chan.name,			\
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
		(chan)->visdn_chan.name,				\
		## arg)

static void hfc_pcm_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "hfc_pcm_chan_release()\n");

	// FIXME
}

static int hfc_pcm_chan_open(struct visdn_chan *visdn_chan)
{
	struct hfc_pcm_chan *chan = to_pcm_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;
	int err;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_pcm_chan(chan, 1, "open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (chan->id != D && chan->id != E &&
	    chan->id != B1 && chan->id != B2) {
		err = -ENOTSUPP;
		goto err_invalid_chan;
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_pcm_chan(chan, 1, "channel opened.\n");

	return 0;

	chan->status = HFC_ST_CHAN_STATUS_FREE;
//err_invalid_l1_proto:
err_invalid_chan:
err_channel_busy:
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static int hfc_pcm_chan_close(struct visdn_chan *visdn_chan)
{
	int err;
	struct hfc_pcm_chan *chan = to_pcm_chan(visdn_chan);
	struct hfc_card *card = chan->port->card;

	if (visdn_chan_lock_interruptible(visdn_chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	chan->status = HFC_PCM_CHAN_STATUS_FREE;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);

	hfc_debug_pcm_chan(chan, 1, "channel closed.\n");

	return 0;

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_chan);
err_visdn_chan_lock:

	return err;
}

static int hfc_pcm_chan_start(struct visdn_chan *visdn_chan)
{
	struct hfc_pcm_chan *chan = to_pcm_chan(visdn_chan);

	hfc_debug_pcm_chan(chan, 1, "channel started\n");

	return 0;
}

static int hfc_pcm_chan_stop(struct visdn_chan *visdn_chan)
{
	struct hfc_pcm_chan *chan = to_pcm_chan(visdn_chan);

	hfc_debug_pcm_chan(chan, 1, "channel stopped\n");

	return 0;
}

static int hfc_pcm_chan_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct hfc_pcm_chan *chan = to_pcm_chan(visdn_leg->chan);
//	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = chan->port->card;
	int err;

	hfc_debug_pcm_chan(chan, 2, "connecting to %s\n",
		visdn_leg2->chan->kobj.name);

	if (visdn_chan_lock_interruptible(visdn_leg->chan)) {
		err = -ERESTARTSYS;
		goto err_visdn_chan_lock;
	}

	hfc_card_lock(card);

	if (chan->status != HFC_ST_CHAN_STATUS_FREE) {
		hfc_debug_pcm_chan(chan, 1, "open failed: channel busy\n");
		err = -EBUSY;
		goto err_channel_busy;
	}

	if (visdn_leg2->chan->chan_class == &hfc_sys_chan_class) {
		chan->connected_sys_chan = to_sys_chan(visdn_leg2->chan);
	} else if (visdn_leg2->chan->chan_class == &hfc_st_chan_class) {
		chan->connected_st_chan = to_st_chan(visdn_leg2->chan);
	} else {
		WARN_ON(1);
	}

	hfc_card_unlock(card);
	visdn_chan_unlock(visdn_leg->chan);

	hfc_debug_pcm_chan(chan, 1, "channel opened.\n");

	return 0;

	chan->status = HFC_ST_CHAN_STATUS_FREE;
//err_invalid_l1_proto:
err_channel_busy:
	visdn_chan_unlock(visdn_leg->chan);
err_visdn_chan_lock:
	hfc_card_unlock(card);

	return err;
}

static void hfc_pcm_chan_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
printk(KERN_INFO "hfc-4s chan %s disconnected\n",
		visdn_leg1->chan->kobj.name);
}

/*static int hfc_pcm_chan_update_parameters(
	struct visdn_chan *chan,
	struct visdn_chan_pars *pars)
{
	// TODO: Complain if someone tryies to change l1_proto mode or bitrate

	memcpy(&chan->pars, pars, sizeof(chan->pars));

	return 0;
}*/

struct visdn_chan_ops hfc_pcm_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= hfc_pcm_chan_release,
	.open			= hfc_pcm_chan_open,
	.close			= hfc_pcm_chan_close,
	.start			= hfc_pcm_chan_start,
	.stop			= hfc_pcm_chan_stop,
};

struct visdn_leg_ops hfc_pcm_chan_leg_ops = {
	.owner			= THIS_MODULE,

	.connect		= hfc_pcm_chan_connect,
	.disconnect		= hfc_pcm_chan_disconnect,
};

struct visdn_chan_class hfc_pcm_chan_class =
{
	.name	= "pcm"
};

void hfc_pcm_chan_init(
	struct hfc_pcm_chan *chan,
	struct hfc_pcm_port *port,
	const char *name,
	int id,
	int hw_index)
{
	chan->port = port;
	chan->status = HFC_ST_CHAN_STATUS_FREE;
	chan->id = id;
	chan->hw_index = hw_index;

	visdn_chan_init(&chan->visdn_chan);

	chan->visdn_chan.ops = &hfc_pcm_chan_ops;
	chan->visdn_chan.chan_class = &hfc_pcm_chan_class;
	chan->visdn_chan.port = &port->visdn_port;

	chan->visdn_chan.leg_a.cxc = &port->card->cxc.visdn_cxc;
	chan->visdn_chan.leg_a.ops = &hfc_pcm_chan_leg_ops;
	chan->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_a.mtu = -1;

	chan->visdn_chan.leg_b.cxc = NULL;
	chan->visdn_chan.leg_b.ops = NULL;
	chan->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	chan->visdn_chan.leg_b.mtu = -1;

	strncpy(chan->visdn_chan.name, name, sizeof(chan->visdn_chan.name));
	chan->visdn_chan.driver_data = chan;
}

int hfc_pcm_chan_register(
	struct hfc_pcm_chan *chan)
{
	int err;

	err = visdn_chan_register(&chan->visdn_chan);
	if (err < 0)
		goto err_chan_register;

//	hfc_pcm_chan_sysfs_create_files(chan);

	return 0;

err_chan_register:

	return err;
}

void hfc_pcm_chan_unregister(
	struct hfc_pcm_chan *chan)
{
//	hfc_pcm_chan_sysfs_delete_files(chan);

	visdn_chan_unregister(&chan->visdn_chan);
}

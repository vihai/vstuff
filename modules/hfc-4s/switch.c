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
#include <linux/init.h>
#include <linux/autoconf.h>
#include <linux/module.h>

#include <linux/kstreamer/node.h>

#include "switch.h"
#include "card.h"

static void hfc_switch_release(struct ks_node *ks_node)
{
	struct hfc_switch *hfcswitch =
		container_of(ks_node, struct hfc_switch, ks_node);

	printk(KERN_DEBUG "hfc_switch_release()\n");

	hfc_card_put(hfcswitch->card);
}

static int hfc_switch_connect(
	struct ks_node *ks_node,
	struct ks_chan *from,
	struct ks_chan *to)
{
//	struct hfc_switch *hfcswitch = to_hfc_switch(ks_node);
//	struct hfc_card *card = hfcswitch->card;

printk(KERN_CRIT "%s/%s connected %s/%s to %s/%s\n",
	ks_node->kobj.parent->name,
	ks_node->kobj.name,
	from->kobj.parent->name,
	from->kobj.name,
	to->kobj.parent->name,
	to->kobj.name);

/*	hfc_card_lock(card);

	if (from->ops == &hfc_st_chan_rx_chan_ops) {
		struct hfc_st_rx_chan* chan_rx = to_st_chan_rx(from);

		if (to->ops == &hfc_st_chan_tx_chan_ops) {

		} else  if (to->ops == &hfc_pcm_chan_tx_chan_ops) {
		} else  if (to->ops == &hfc_sys_chan_tx_chan_ops) {
		}

	} else if (from->ops == &hfc_pcm_chan_rx_chan_ops) {
		struct hfc_pcm_chan_rx* chan_rx = to_pcm_chan_rx(from);

		if (to->ops == &hfc_st_chan_tx_chan_ops) {
		} else  if (to->ops == &hfc_pcm_chan_tx_chan_ops) {
		} else  if (to->ops == &hfc_sys_chan_tx_chan_ops) {
		}

	} else if (from->ops == &hfc_sys_chan_rx_chan_ops) {
		struct hfc_sys_chan_rx* chan_rx = to_sys_chan_rx(from);

		if (to->ops == &hfc_st_chan_tx_chan_ops) {
			chan_rx->
		} else  if (to->ops == &hfc_pcm_chan_tx_chan_ops) {
		} else  if (to->ops == &hfc_sys_chan_tx_chan_ops) {
		}

	} else
		BUG();

	hfc_card_unlock(card);*/

	return 0;

//	hfc_card_unlock(card);

//	return err;
}

static void hfc_switch_disconnect(
	struct ks_node *hfcswitch,
	struct ks_chan *from,
	struct ks_chan *to)
{
}

static struct ks_node_ops hfc_switch_ops =
{
	.owner		= THIS_MODULE,
	.release	= hfc_switch_release,

	.connect	= hfc_switch_connect,
	.disconnect	= hfc_switch_disconnect,
};

struct hfc_switch *hfc_switch_create(
	struct hfc_switch *hfcswitch,
	struct hfc_card *card)
{
	hfcswitch->card = card;

	ks_node_create(&hfcswitch->ks_node, &hfc_switch_ops,
			"hfc-switch",
			&card->pci_dev->dev.kobj);

	return hfcswitch;
}

int hfc_switch_register(struct hfc_switch *hfcswitch)
{
	int err;

	err = ks_node_register(&hfcswitch->ks_node);
	if (err < 0)
		goto err_node_register;

	return 0;

err_node_register:

	return err;
}

void hfc_switch_unregister(struct hfc_switch *hfcswitch)
{
	ks_node_unregister(&hfcswitch->ks_node);
}

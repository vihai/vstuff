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

#ifndef _HFC_PCM_PORT_H
#define _HFC_PCM_PORT_H

#include <linux/visdn/core.h>
#include <linux/visdn/port.h>

#include "util.h"
#include "pcm_chan.h"

#define to_pcm_port(port) container_of(port, struct hfc_pcm_port, visdn_port)

#ifdef DEBUG_CODE
#define hfc_debug_pcm_port(port, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"pcm%d:"				\
			format,					\
			(port)->card->pcidev->dev.bus->name,	\
			(port)->card->pcidev->dev.bus_id,	\
			(port)->hw_index,			\
			## arg)
#else
#define hfc_debug_pcm_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_pcm_port(port, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s-%s:"				\
		"pcm%d:"				\
		format,					\
		(port)->card->pcidev->dev.bus->name,	\
		(port)->card->pcidev->dev.bus_id,	\
		(port)->hw_index,			\
		## arg)

struct hfc_pcm_port
{
	struct hfc_card *card;

	int hw_index;

	int master;
	int c4_pol_positive;
	int f0_negative;
	int f0_len;
	int pll_adj;

	int bitrate;

	int num_chans;
	struct hfc_pcm_chan chans[128];

	struct visdn_port visdn_port;
};

void hfc_pcm_port_configure(struct hfc_pcm_port *port, int slots);

void hfc_pcm_port_init(
	struct hfc_pcm_port *port,
	struct hfc_card *card,
	const char *name);
int hfc_pcm_port_register(
	struct hfc_pcm_port *port);
void hfc_pcm_port_unregister(
	struct hfc_pcm_port *port);

#endif

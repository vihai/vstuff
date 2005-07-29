/*
 *
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _HFC_PCM_PORT_H
#define _HFC_PCM_PORT_H

#include <visdn.h>

#define to_pcm_port(port) container_of(port, struct hfc_pcm_port, visdn_port)

#ifdef DEBUG
#define hfc_debug_pcm_port(port, dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s:"					\
			"pcm%d:"				\
			format,					\
			(port)->card->pcidev->dev.bus_id,	\
			(port)->hw_index,			\
			## arg)
#else
#define hfc_debug_pcm_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_pcm_port(port, level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"%s:"					\
		"pcm%d:"				\
		format,					\
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

	int data_rate;

	struct visdn_port visdn_port;
};

extern struct visdn_port_ops hfc_pcm_port_ops;

#endif

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

#ifndef _HFC_PORT_H
#define _HFC_PORT_H

#include <visdn.h>

#include "chan.h"
//#include "fifo.h"

#ifdef DEBUG
#define hfc_debug_port(dbglevel, port, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"port: %d "			\
			format,				\
			port->card->id, port->id, ## arg)
#else
#define hfc_debug_port(dbglevel, port, format, arg...) do {} while (0)
#endif

#define hfc_msg_port(level, port, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"port: %d "				\
		format,					\
		port->card->id, port->id, ## arg)

#define D 0
#define B1 1
#define B2 2
#define E 3
#define SQ 4

struct hfc_st_port
{
	struct hfc_card *card;

	int id;

//	int enabled;
	u8 l1_state;
	int clock_delay;
	int sampling_comp;

	struct hfc_chan_duplex chans[5];

	// This struct contains a copy of some registers whose bits may be
	// changed independently.
	struct
	{
		u8 st_ctrl_0;
		u8 st_ctrl_2;
		u8 st_clk_dly;
	} regs;

	struct visdn_port visdn_port;
};

#ifdef DEBUG
#define hfc_debug_pcm_port(dbglevel, port, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			"pcm: "			\
			format,				\
			port->card->id, ## arg)
#else
#define hfc_debug_pcm_port(dbglevel, port, format, arg...) do {} while (0)
#endif

#define hfc_msg_pcm_port(level, port, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"pcm: "				\
		format,					\
		port->card->id, ## arg)

struct hfc_pcm_port
{
	struct hfc_card *card;

	int master;
	int c4_pol_positive;
	int f0_negative;
	int f0_len;
	int pll_adj;

	int data_rate;

	struct visdn_port visdn_port;
};

#endif

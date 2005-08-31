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

#ifndef _HFC_ST_PORT_H
#define _HFC_ST_PORT_H

#include <visdn.h>

#include "chan.h"

#define hfc_D_CHAN_OFF 2
#define hfc_B1_CHAN_OFF 0
#define hfc_B2_CHAN_OFF 1
#define hfc_E_CHAN_OFF 3

#define to_st_port(port) container_of(port, struct hfc_st_port, visdn_port)

#ifdef DEBUG
#define hfc_debug_port(port, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"st%d:"					\
			format,					\
			(port)->card->pcidev->dev.bus->name,	\
			(port)->card->pcidev->dev.bus_id,	\
			(port)->id,				\
			## arg)
#else
#define hfc_debug_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_port(port, level, format, arg...)		\
	printk(level hfc_DRIVER_PREFIX				\
		"%s-%s:"					\
		"st%d:"						\
		format,						\
		(port)->card->pcidev->dev.bus->name,		\
		(port)->card->pcidev->dev.bus_id,		\
		(port)->id,					\
		## arg)

#define D 0
#define B1 1
#define B2 2
#define E 3
#define SQ 4

#define HFC_DEF_NT_CLK_DLY 0x0C
#define HFC_DEF_NT_SAMPL_COMP 0x6
#define HFC_DEF_TE_CLK_DLY 0x0E
#define HFC_DEF_TE_SAMPL_COMP 0x6

struct hfc_st_port
{
	struct hfc_card *card;

	int id;

	BOOL nt_mode;
	BOOL sq_enabled;
	u8 l1_state;
	int clock_delay;
	int sampling_comp;

	struct hfc_chan_duplex chans[5];

	// This struct contains a copy of some registers whose bits may be
	// changed independently.

	struct work_struct state_change_work;

	struct visdn_port visdn_port;
};

void hfc_st_port_check_l1_up(struct hfc_st_port *port);
void hfc_st_port_update_st_ctrl_0(struct hfc_st_port *port);
void hfc_st_port_update_st_ctrl_2(struct hfc_st_port *port);
void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port);

void hfc_st_port_init(
	struct hfc_st_port *port,
	struct hfc_card *card,
	int id);

#endif

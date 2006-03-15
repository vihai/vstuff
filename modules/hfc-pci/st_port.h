/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_ST_PORT_H
#define _HFC_ST_PORT_H

#include <linux/visdn/port.h>

#include "st_chan.h"

#define to_st_port(port) container_of(port, struct hfc_st_port, visdn_port)

#ifdef DEBUG_CODE
#define hfc_debug_st_port(port, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"%s-%s:"				\
			"st:"					\
			format,					\
			(port)->card->pci_dev->dev.bus->name,	\
			(port)->card->pci_dev->dev.bus_id,	\
			## arg)
#else
#define hfc_debug_st_port(port, dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg_port(port, level, format, arg...)		\
	printk(level hfc_DRIVER_PREFIX				\
		"%s-%s:"					\
		"st:"						\
		format,						\
		(port)->card->pci_dev->dev.bus->name,		\
		(port)->card->pci_dev->dev.bus_id,		\
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

	BOOL nt_mode;
	BOOL sq_enabled;

	int clock_delay;
	int sampling_comp;

	u8 l1_state;
	BOOL rechecking_f7_f6;

	struct timer_list timer_t1;
	unsigned int timer_t1_value;

	struct timer_list timer_t3;
	unsigned int timer_t3_value;

	struct hfc_st_chan chans[5];

	int echo_enabled;

	struct work_struct state_change_work;

	struct visdn_port visdn_port;
};

extern void hfc_st_port_update_sctrl(struct hfc_st_port *port);
extern void hfc_st_port_update_sctrl_r(struct hfc_st_port *port);
extern void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port);

extern void hfc_st_port_init(
	struct hfc_st_port *port,
	struct hfc_card *card,
	const char *name);

extern int hfc_st_port_register(struct hfc_st_port *port);
extern void hfc_st_port_unregister(struct hfc_st_port *port);

#endif

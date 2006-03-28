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

#ifndef _HFC_ST_PORT_H
#define _HFC_ST_PORT_H

#include <linux/visdn/core.h>
#include <linux/visdn/port.h>

#include "st_chan.h"

#define hfc_D_CHAN_OFF 2
#define hfc_B1_CHAN_OFF 0
#define hfc_B2_CHAN_OFF 1
#define hfc_E_CHAN_OFF 3

#define to_st_port(port) container_of(port, struct hfc_st_port, visdn_port)

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
	int clock_delay;
	int sampling_comp;
	BOOL enable_96khz;

	u8 l1_state;
	BOOL rechecking_f7_f6;

	struct timer_list timer_t1;
	unsigned int timer_t1_value;

	struct timer_list timer_t3;
	unsigned int timer_t3_value;

	struct hfc_st_chan chans[5];

	struct hfc_led *led;

	struct work_struct state_change_work;

	struct visdn_port visdn_port;
};

void hfc_st_port_check_l1_up(struct hfc_st_port *port);
void hfc_st_port_update_st_ctrl0(struct hfc_st_port *port);
void hfc_st_port_update_st_ctrl2(struct hfc_st_port *port);
void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port);

void hfc_st_port_init(
	struct hfc_st_port *port,
	struct hfc_card *card,
	const char *name,
	int id,
	struct hfc_led *led);
int hfc_st_port_register(
	struct hfc_st_port *port);
void hfc_st_port_unregister(
	struct hfc_st_port *port);

#endif

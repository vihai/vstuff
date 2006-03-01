/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_E1_PORT_H
#define _HFC_E1_PORT_H

#include <linux/visdn/core.h>
#include <linux/visdn/port.h>

#include "e1_chan.h"

#define to_e1_port(port) container_of(port, struct hfc_e1_port, visdn_port)

enum hfc_line_code
{
	hfc_LINE_CODE_NRZ,
	hfc_LINE_CODE_HDB3,
	hfc_LINE_CODE_AMI,
};

enum hfc_clock_source
{
	hfc_CLOCK_SOURCE_LOOP,
	hfc_CLOCK_SOURCE_F0IO,
	hfc_CLOCK_SOURCE_SYNC_IN,
};

struct hfc_e1_port
{
	struct hfc_card *card;

	BOOL nt_mode;

	u8 l1_state;

	enum hfc_clock_source clock_source;

	enum hfc_line_code rx_line_code;
	BOOL rx_full_baud;
	BOOL rx_crc4;
	enum hfc_line_code tx_line_code;
	BOOL tx_full_baud;
	BOOL tx_crc4;

	struct hfc_e1_chan chans[32];

	struct work_struct state_change_work;
	struct work_struct fifo_activation_work;
	struct work_struct counters_update_work;

	struct visdn_port visdn_port;

	u64 fas_cnt;
	u64 vio_cnt;
	u64 crc_cnt;
	u64 e_cnt;
	u64 sa6_val13_cnt;
	u64 sa6_val23_cnt;
};

void hfc_e1_port_init(
	struct hfc_e1_port *port,
	struct hfc_card *card,
	const char *name);

int hfc_e1_port_register(
	struct hfc_e1_port *port);
void hfc_e1_port_unregister(
	struct hfc_e1_port *port);

void hfc_e1_port_update_rx0(struct hfc_e1_port *port);
void hfc_e1_port_update_tx0(struct hfc_e1_port *port);
void hfc_e1_port_update_rx_sl0_cfg1(struct hfc_e1_port *port);
void hfc_e1_port_update_tx_sl0_cfg1(struct hfc_e1_port *port);
void hfc_e1_port_update_sync_ctrl( struct hfc_e1_port *port);

#endif

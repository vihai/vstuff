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

#ifndef _HFC_SYS_PORT_H
#define _HFC_SYS_PORT_H

#include <linux/kstreamer/kstreamer.h>
#include <linux/visdn/port.h>

#include "util.h"
#include "sys_chan.h"

#define to_sys_port(port) container_of(port, struct hfc_sys_port, visdn_port)

struct hfc_sys_port
{
	struct hfc_card *card;

	int num_chans;
	struct hfc_sys_chan chans[32];

	struct visdn_port visdn_port;
};

struct hfc_fifo_zone_config
{
	u16 z_min;
	u16 z_max;
};

struct hfc_fifo_config
{
	char v_ram_sz;
	char v_fifo_md;
	char v_fifo_sz;

	u8 f_min;
	u8 f_max;

	int zone2_start_id;
	int num_fifos;

	struct hfc_fifo_zone_config zone1;
	struct hfc_fifo_zone_config zone2;
};

void hfc_sys_port_configure(
	struct hfc_sys_port *port,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz);
void hfc_sys_port_reconfigure(
	struct hfc_sys_port *port,
	int v_ram_sz,
	int v_fifo_md,
	int v_fifo_sz);

void hfc_sys_port_update_fsm(
	struct hfc_sys_port *port);

void hfc_sys_port_init(
	struct hfc_sys_port *port,
	struct hfc_card *card,
	const char *name);
int hfc_sys_port_register(
	struct hfc_sys_port *port);
void hfc_sys_port_unregister(
	struct hfc_sys_port *port);

static inline struct hfc_sys_port *hfc_sys_port_get(struct hfc_sys_port *port)
{
	visdn_port_get(&port->visdn_port);

	return port;
}

static inline void hfc_sys_port_put(struct hfc_sys_port *port)
{
	visdn_port_put(&port->visdn_port);
}

#endif

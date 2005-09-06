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

#ifndef _HFC_E1_PORT_H
#define _HFC_E1_PORT_H

#include <visdn.h>

#include "chan.h"
//#include "fifo.h"

#define to_e1_port(port) container_of(port, struct hfc_e1_port, visdn_port)

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

enum hfc_e1_port_code
{
	HFC_E1_PORT_CODE_NRZ,
	HFC_E1_PORT_CODE_AMI,
	HFC_E1_PORT_CODE_HDB3,
};

enum hfc_e1_port_ais_mode
{
	HFC_E1_PORT_AIS_MODE_ETS_300_233
	HFC_E1_PORT_AIS_MODE_ITU_T_G_776
};

struct hfc_e1_port
{
	struct hfc_card *card;

	int id;

//	int enabled;
	u8 l1_state;
	enum hfc_e1_port_code rx_line_code;
	enum hfc_e1_port_code tx_line_code;
	BOOL rx_full_bauded;
	BOOL tx_full_bauded;
	BOOL rx_cmi_enabled;
	BOOL tx_cmi_enabled;
	BOOL rx_cmi_inverted;
	BOOL tx_cmi_inverted;
	BOOL rx_invert_clock;
	BOOL tx_invert_clock;
	BOOL rx_invert_data;
	BOOL tx_invert_data;
	enum hfc_e1_port_ais_mode ais_mode;

	BOOL rx_crc4;
	BOOL tx_crc4;

	struct hfc_chan_duplex chans[32];

	struct work_struct state_change_work;

	struct visdn_port visdn_port;
};

extern struct visdn_port_ops hfc_e1_port_ops;

void hfc_e1_port_check_l1_up(struct hfc_e1_port *port);
void hfc_e1_port__do_set_role(struct hfc_e1_port *port, int nt_mode);
void hfc_e1_port_state_change_work(void *data);

#endif

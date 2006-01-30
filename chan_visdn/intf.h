/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _INTF_H
#define _INTF_H

#include <linux/if.h>

#include <libq931/q931.h>

#include "ton.h"

enum visdn_clir_mode
{
	VISDN_CLIR_MODE_OFF,
	VISDN_CLIR_MODE_DEFAULT_ON,
	VISDN_CLIR_MODE_DEFAULT_OFF,
	VISDN_CLIR_MODE_ON,
};

struct visdn_clip_number
{
	struct list_head node;
	char number[32];
};

struct visdn_interface
{
	struct list_head ifs_node;

	int refcnt;
	ast_mutex_t lock;

	char name[IFNAMSIZ];

	int configured;
	int open_pending;

	enum q931_interface_network_role network_role;
	enum visdn_type_of_number outbound_called_ton;
	char force_outbound_cli[32];
	enum visdn_type_of_number force_outbound_cli_ton;

	int cli_rewriting;
	char national_prefix[10];
	char international_prefix[10];
	char network_specific_prefix[10];
	char subscriber_prefix[10];
	char abbreviated_prefix[10];

	int tones_option;
	char context[AST_MAX_EXTENSION];

	int clip_enabled;
	int clip_override;
	char clip_default_name[128];
	char clip_default_number[32];
	struct list_head clip_numbers_list;
	int clip_special_arrangement;
	enum visdn_clir_mode clir_mode;
	int overlap_sending;
	int overlap_receiving;
	int call_bumping;
	int dlc_autorelease_time;

	int echocancel;
	int echocancel_taps;

	int T301;
	int T302;
	int T303;
	int T304;
	int T305;
	int T306;
	int T307;
	int T308;
	int T309;
	int T310;
	int T312;
	int T313;
	int T314;
	int T316;
	int T317;
	int T318;
	int T319;
	int T320;
	int T321;
	int T322;

	struct list_head suspended_calls;

	struct q931_interface *q931_intf;

	char remote_port[PATH_MAX];
};

struct visdn_interface *visdn_intf_get(struct visdn_interface *intf);
void visdn_intf_put(struct visdn_interface *intf);
struct visdn_interface *visdn_intf_get_by_name(const char *name);

int visdn_intf_open(struct visdn_interface *intf);

void visdn_intf_reload(struct ast_config *cfg);

int visdn_intf_clip_valid(
	struct visdn_interface *intf,
	const char *called_number);

void visdn_intf_cli_register(void);
void visdn_intf_cli_unregister(void);

#endif

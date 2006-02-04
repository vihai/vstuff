/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_INTF_H
#define _LIBQ931_INTF_H

#include <netinet/in.h>

#include <linux/lapd.h>

#include <libq931/list.h>
#include <libq931/channel.h>
#include <libq931/timer.h>
#include <libq931/dlc.h>
#include <libq931/callref.h>
#include <libq931/call.h>
#include <libq931/global.h>

#define report_intf(intf, lvl, format, arg...)		\
	q931_report((lvl),				\
		"%s: "					\
		format,					\
		(intf)->name,				\
		## arg)

enum q931_interface_network_role
{
	Q931_INTF_NET_USER,
	Q931_INTF_NET_PRIVATE,
	Q931_INTF_NET_LOCAL,
	Q931_INTF_NET_TRANSIT,
	Q931_INTF_NET_INTERNATIONAL,
};

struct q931_call;
struct q931_interface
{
	struct list_head node;

	char *name;

	int flags;

	enum lapd_intf_type type;
	enum lapd_intf_mode mode;
	enum lapd_intf_role role;

	enum q931_interface_network_role network_role;

	// Accept master_socket in NT mode
	int master_socket;

	// Broadcast DLC for multipoint interfaces in NT MODE
	struct q931_dlc bc_dlc;

	// Interface's PTP DLC for TE mode
	struct q931_dlc dlc;

	int dlc_autorelease_time;

	int enable_bumping;

	struct list_head dlcs;

	q931_callref next_call_reference;
	int call_reference_len;

	int ncalls;
	// TODO: Use a HASH for improved scalability
	struct list_head calls;

	struct q931_channel channels[32];
	int n_channels;

	struct q931_global_call global_call;

	longtime_t T301;
	longtime_t T302;
	longtime_t T303;
	longtime_t T304;
	longtime_t T305;
	longtime_t T306;
	longtime_t T308;
	longtime_t T309;
	longtime_t T310;
	longtime_t T312;
	longtime_t T313;
	longtime_t T314;
	longtime_t T316;
	longtime_t T317;
	longtime_t T318;
	longtime_t T319;
	longtime_t T320;
	longtime_t T321;
	longtime_t T322;

	void *pvt;
};

inline static void q931_intf_add_call(
	struct q931_interface *intf,
	struct q931_call *call)
{
	list_add_tail(&call->calls_node, &intf->calls);
	intf->ncalls++;
}

inline static void q931_intf_del_call(
	struct q931_call *call)
{
	list_del(&call->calls_node);
	call->intf->ncalls--;
}

#define Q931_INTF_FLAGS_DEBUG (1 << 0)

struct q931_interface *q931_intf_open(
	const char *name,
	int flags);
void q931_intf_close(struct q931_interface *intf);

q931_callref q931_intf_find_free_call_reference(struct q931_interface *intf);

#endif

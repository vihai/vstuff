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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/output.h>
#include <libq931/ces.h>
#include <libq931/call.h>
#include <libq931/dlc.h>
#include <libq931/intf.h>

q931_callref q931_intf_find_free_call_reference(
	struct q931_interface *interface)
{
	assert(interface);

	q931_callref call_reference;
	q931_callref first_call_reference;

	first_call_reference = interface->next_call_reference;

try_again:

	call_reference = interface->next_call_reference;

	interface->next_call_reference++;

	if (interface->next_call_reference >=
	    (1 << ((interface->call_reference_len * 8) - 1)))
		interface->next_call_reference = 1;

	struct q931_call *call;
	list_for_each_entry(call, &interface->calls, calls_node) {
		if (call->direction == Q931_CALL_DIRECTION_OUTBOUND &&
		    call->call_reference == call_reference) {
			if (call_reference == interface->next_call_reference)
				return -1;
			else
				goto try_again;
		}
	}

	return call_reference;
}

int q931_open_socket(struct q931_interface *intf)
{
	int s = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (s < 0)
		goto err_socket;

	if (intf->flags & Q931_INTF_FLAGS_DEBUG) {
		int on=1;

		setsockopt(s, SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
	}

	if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
			intf->name, strlen(intf->name)+1) < 0) {

		report_intf(intf, LOG_ERR,
			"setsockopt(SO_BINDTODEVICE): %s\n",
			strerror(errno));

		goto err_setsockopt;
	}

	int oldflags;
	oldflags = fcntl(s, F_GETFL, 0);
	if (oldflags < 0) {
		report_intf(intf, LOG_ERR,
			"fcntl(F_GETFL): %s\n",
			strerror(errno));

		goto err_fcntl_getfl;
		goto err_fcntl_setfl;
	}

	if (fcntl(s, F_SETFL, oldflags | O_NONBLOCK) < 0) {
		report_intf(intf, LOG_ERR,
			"fcntl(F_SETFL): %s\n",
			strerror(errno));

		goto err_fcntl_setfl;
	}

	return s;

err_fcntl_setfl:
err_fcntl_getfl:
err_setsockopt:
	close(s);
err_socket:

	return -1;
}

int q931_intf_open_nt(struct q931_interface *intf)
{
	int s;

	s = q931_open_socket(intf);
	if (s < 0)
		goto err_open_accept_sock;

	if (listen(s, 100) < 0) {
		report_intf(intf, LOG_ERR,
			"listen(): %s\n",
			strerror(errno));
		goto err_listen;
	}

	intf->accept_socket = s;

	if (intf->mode == LAPD_INTF_MODE_POINT_TO_POINT) {
		s = q931_open_socket(intf);
		if (s < 0)
			goto err_open_dlc;

		if (intf->tei == LAPD_DYNAMIC_TEI)
			intf->tei = 0;

		struct sockaddr_lapd sal;
		sal.sal_family = AF_LAPD;
		sal.sal_tei = intf->tei;

		if (bind(s, (struct sockaddr *)&sal, sizeof(sal)) < 0) {
			report_intf(intf, LOG_ERR,
				"bind(): %s\n",
				strerror(errno));
			goto err_bind_dlc;
		}

		q931_dlc_init(&intf->dlc, intf, s);
	} else {
		q931_dlc_init(&intf->dlc, NULL, -1);
	}

	intf->T301 = 180 * 1000000LL;
	intf->T302 =  12 * 1000000LL;
	intf->T303 =   4 * 1000000LL;
	intf->T304 =  20 * 1000000LL;
	intf->T305 =  30 * 1000000LL;
	intf->T306 =  30 * 1000000LL;
	intf->T308 =   4 * 1000000LL;
	intf->T309 =   6 * 1000000LL;
	intf->T310 =  35 * 1000000LL;
	intf->T312 = intf->T303 + 2 * 1000000LL;
	intf->T314 =   4 * 1000000LL;
	intf->T316 = 120 * 1000000LL;
	intf->T317 =  60 * 1000000LL;
	intf->T320 =  30 * 1000000LL;
	intf->T321 =  30 * 1000000LL;
	intf->T322 =   4 * 1000000LL;

	return 0;

err_bind_dlc:
err_open_dlc:
err_listen:
err_open_accept_sock:

	return -1;
}

int q931_intf_open_te(struct q931_interface *intf)
{
	int s;

	intf->accept_socket = -1;

	s = q931_open_socket(intf);
	if (s < 0)
		goto err_open_dlc;

	if (intf->mode == LAPD_INTF_MODE_POINT_TO_POINT &&
	    intf->tei == LAPD_DYNAMIC_TEI)
			intf->tei = 0;

	struct sockaddr_lapd sal;
	sal.sal_family = AF_LAPD;
	sal.sal_tei = intf->tei;

	if (bind(s, (struct sockaddr *)&sal, sizeof(sal)) < 0) {
		report_intf(intf, LOG_ERR,
			"bind(t): %s\n",
			strerror(errno));
		goto err_bind_dlc;
	}

	q931_dlc_init(&intf->dlc, intf, s);

	intf->T301 = 180 * 1000000LL;
	intf->T302 =   6 * 1000000LL;
	intf->T303 =   5 * 1000000LL;
	intf->T304 = 999 * 1000000LL; // T304 is disabled
	intf->T305 =  30 * 1000000LL;
	intf->T308 =   4 * 1000000LL;
	intf->T309 =   6 * 1000000LL;
	intf->T310 =  40 * 1000000LL;
	intf->T313 =   4 * 1000000LL;
	intf->T314 =   4 * 1000000LL;
	intf->T316 = 120 * 1000000LL;
	intf->T317 =  60 * 1000000LL;
	intf->T318 =   4 * 1000000LL;
	intf->T319 =   4 * 1000000LL;
	intf->T321 =  30 * 1000000LL;
	intf->T322 =   4 * 1000000LL;

	return 0;

err_bind_dlc:
err_open_dlc:

	return -1;
}

struct q931_interface *q931_intf_open(
	const char *name,
	int flags,
	int tei)
{
	struct q931_interface *intf;
	int err;

	assert(name);

	intf = malloc(sizeof(*intf));
	if (!intf)
		return NULL;

	memset(intf, 0, sizeof(*intf));

	INIT_LIST_HEAD(&intf->calls);
	INIT_LIST_HEAD(&intf->dlcs);

	intf->name = strdup(name);
	intf->dlc_autorelease_time = 0;
	intf->enable_bumping = TRUE;

	intf->flags = flags;

	int s;

	s = q931_open_socket(intf);
	if (s < 0)
		goto err_open_broadcast;

	socklen_t optlen = sizeof(intf->type);
	if (getsockopt(s, SOL_LAPD, LAPD_INTF_TYPE,
					&intf->type, &optlen) < 0) {
		report_intf(intf, LOG_ERR,
			"getsockopt(LAPD_INTF_TYPE): %s\n",
			strerror(errno));
		goto err_getsockopt;
	}

	optlen = sizeof(intf->type);
	if (getsockopt(s, SOL_LAPD, LAPD_INTF_MODE,
					&intf->mode, &optlen) < 0) {
		report_intf(intf, LOG_ERR,
			"getsockopt(LAPD_INTF_MODE): %s\n",
			strerror(errno));
		goto err_getsockopt;
	}

	optlen = sizeof(intf->role);
	if (getsockopt(s, SOL_LAPD, LAPD_INTF_ROLE,
					&intf->role, &optlen) < 0) {
		report_intf(intf, LOG_ERR,
			"getsockopt(LAPD_INTF_ROLE): %s\n",
			strerror(errno));
		goto err_getsockopt;
	}

	struct sockaddr_lapd sal;
	sal.sal_family = AF_LAPD;
	sal.sal_tei = LAPD_BROADCAST_TEI;

	if (bind(s, (struct sockaddr *)&sal, sizeof(sal)) < 0) {
		report_intf(intf, LOG_ERR,
			"bind(broacast): %s\n",
			strerror(errno));
		goto err_bind_broadcast;
	}

	q931_broadcast_dlc_init(&intf->bc_dlc, intf, s);

	if (intf->role == LAPD_INTF_ROLE_NT)
		err = q931_intf_open_nt(intf);
	else
		err = q931_intf_open_te(intf);

	if (err < 0)
		goto err_open;

	switch (intf->type) {
	case LAPD_INTF_TYPE_BRA:
		intf->n_channels = 2;
		intf->call_reference_len = 1;
	break;
	case LAPD_INTF_TYPE_PRA:
		intf->n_channels = 30;
		intf->call_reference_len = 2;
	break;
	}

	intf->next_call_reference =
		(rand() &
		((1 << ((intf->call_reference_len * 8) - 1)) - 2)) + 1;

	int i;
	for (i=0; i<intf->n_channels; i++)
		q931_channel_init(&intf->channels[i], i, intf);

	q931_global_init(&intf->global_call, intf);

	list_add(&intf->node, &q931_interfaces);

	return intf;

err_open:
err_bind_broadcast:
err_getsockopt:
err_open_broadcast:
	free(intf);

	return NULL;
}

void q931_intf_close(struct q931_interface *intf)
{
	assert(intf);

	list_del(&intf->node);

	if (intf->dlc.socket >= 0) {
		shutdown(intf->dlc.socket, 2);
		close(intf->dlc.socket);
	}

	if (intf->bc_dlc.socket >= 0) {
		shutdown(intf->bc_dlc.socket, 2);
		close(intf->bc_dlc.socket);
	}

	struct q931_dlc *dlc, *tpos;
	list_for_each_entry_safe(dlc, tpos,
			&intf->dlcs, intf_node) {

		shutdown(dlc->socket, 2);
		close(dlc->socket);

		list_del(&dlc->intf_node);

		free(dlc);
	}

	if (intf->accept_socket >= 0)
		close(intf->accept_socket);

	if (intf->name)
		free(intf->name);

	free(intf);
}

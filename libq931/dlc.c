/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#define Q931_PRIVATE

#include <unistd.h>
#include <assert.h>

#include <libq931/list.h>
#include <libq931/lib.h>
#include <libq931/intf.h>
#include <libq931/logging.h>
#include <libq931/dlc.h>

void _q931_dlc_hold(
	struct q931_dlc *dlc,
	const char *file,
	int line)
{
	assert(dlc);
	assert(dlc->holdcnt >= 0);
	assert(dlc->tei != LAPD_BROADCAST_TEI);

	dlc->holdcnt++;

#if 1
	report_dlc(dlc, LOG_DEBUG, "%s:%d HOLD (%d => %d)\n",
		file, line,
		dlc->refcnt, dlc->refcnt+1);
#endif

	if (dlc->holdcnt == 1) {
		q931_stop_timer(&dlc->autorelease_timer);

		report_dlc(dlc, LOG_DEBUG, "DLC autorelease timer stopped\n");
	}
}

void _q931_dlc_release(
	struct q931_dlc *dlc,
	const char *file,
	int line)
{
	assert(dlc);
	assert(dlc->holdcnt > 0);
	assert(dlc->tei != LAPD_BROADCAST_TEI);

	dlc->holdcnt--;

	if (dlc->holdcnt == 0) {
		if (dlc->intf->dlc_autorelease_time) {
			q931_start_timer_delta(
				&dlc->autorelease_timer,
				dlc->intf->dlc_autorelease_time * 1000000LL);

			report_dlc(dlc, LOG_DEBUG,
				"DLC autorelease timer started\n");
		}
	}
}

struct q931_dlc *_q931_dlc_get(
	struct q931_dlc *dlc,
	const char *file,
	int line)
{
	assert(dlc);
	assert(dlc->refcnt > 0);

#if 1
	report_dlc(dlc, LOG_DEBUG, "%s:%d GET (%d => %d)\n",
		file, line,
		dlc->refcnt, dlc->refcnt+1);
#endif

	dlc->refcnt++;

	return dlc;
}

void _q931_dlc_put(
	struct q931_dlc *dlc,
	const char *file,
	int line)
{
	assert(dlc);
	assert(dlc->refcnt > 0);
	assert(dlc->intf);

#if 1
	report_dlc(dlc, LOG_DEBUG, "%s:%d PUT (%d => %d)\n",
		file, line,
		dlc->refcnt, dlc->refcnt-1);
#endif

	dlc->refcnt--;

	if (dlc->refcnt == 0) {
		report_dlc(dlc, LOG_DEBUG, "Releasing DLC\n");

		free(dlc);
	}
}

static void q931_dlc_autorelease(void *data)
{
	assert(data);

	struct q931_dlc *dlc = data;

	report_dlc(dlc, LOG_DEBUG, "DLC autorelease timer fired\n");

	shutdown(dlc->socket, 2);
}

void q931_dlc_init(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket)
{
	memset(dlc, 0, sizeof(*dlc));

	dlc->holdcnt = 0;
	dlc->refcnt = 1;

	INIT_LIST_HEAD(&dlc->intf_node);

	q931_init_timer(&dlc->autorelease_timer, "dlc-autorelease",
			q931_dlc_autorelease, dlc);

	dlc->socket = socket;
	dlc->intf = intf;
	dlc->status = Q931_DLC_STATUS_DISCONNECTED;
	dlc->tei = 0;

	INIT_LIST_HEAD(&dlc->outgoing_queue);
}

void q931_broadcast_dlc_init(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket)
{
	dlc->socket = socket;
	dlc->intf = intf;
	dlc->tei = LAPD_BROADCAST_TEI;
}

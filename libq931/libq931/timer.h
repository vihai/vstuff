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

#ifndef _LIBQ931_TIMER_H
#define _LIBQ931_TIMER_H

#include <libq931/list.h>

typedef long long longtime_t;

struct q931_timer
{
	struct list_head node;

	const char *name;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(void *data);
};

extern longtime_t q931_run_timers();

extern longtime_t q931_longtime_now(void);

#ifdef Q931_PRIVATE

extern void q931_init_timer(
	struct q931_timer *timer,
	const char *name,
	void (*func)(void *data),
	void *data);

extern void q931_start_timer(
	struct q931_timer *timer,
	longtime_t expires);

extern void q931_start_timer_delta(
	struct q931_timer *timer,
	longtime_t delta);

extern void q931_stop_timer(struct q931_timer *timer);

static inline int q931_timer_pending(struct q931_timer *timer)
{
	return timer->pending;
}

#endif

#endif

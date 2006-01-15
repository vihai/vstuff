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

#include <string.h>
#include <sys/time.h>

#define Q931_PRIVATE

#include <libq931/util.h>
#include <libq931/lib.h>
#include <libq931/list.h>
#include <libq931/timer.h>

void q931_init_timer(
	struct q931_timer *timer,
	void (*func)(void *data),
	void *data)
{
	timer->expires = 0LL;
	timer->func = func;
	timer->data = data;
	timer->pending = 0;
}

void q931_start_timer(
	struct q931_timer *timer,
	longtime_t expires)
{
	timer->expires = expires;

	if (!timer->pending) {
		list_add_tail(&timer->node, &q931_timers);

		timer->pending = TRUE;
	}

	if (q931_timer_update)
		q931_timer_update();
}

longtime_t q931_longtime_now()
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

void q931_start_timer_delta(
	struct q931_timer *timer,
	longtime_t delta)
{
	q931_start_timer(timer, q931_longtime_now() + delta);
}

void q931_stop_timer(struct q931_timer *timer)
{
	if (timer->pending) {
		list_del(&timer->node);

		timer->pending = FALSE;
	}
}

longtime_t q931_run_timers(void)
{
	longtime_t now = q931_longtime_now();
	longtime_t next_timer = -1;
	BOOL next_timer_set = FALSE;

	do {
		struct q931_timer *timer, *t;
retry:
		list_for_each_entry_safe(timer, t, &q931_timers, node) {
			if (timer->expires < now) {
				list_del(&timer->node);
				timer->pending = FALSE;

				timer->func(timer->data);

				goto retry;
			}

			if (timer->pending &&
			   (!next_timer_set || timer->expires < next_timer)) {
				next_timer = timer->expires;
				next_timer_set = TRUE;
			}
		}
	} while (0);

	if (next_timer == -1)
		return -1;
	else
		return next_timer - now;
}


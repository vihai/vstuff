/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_TIMER_H
#define _KS_TIMER_H

#include <pthread.h>

#include <list.h>
#include <longtime.h>

struct ks_timerset
{
	struct list_head timers;
	pthread_mutex_t timers_lock;

	void *data;

	void (*timers_updated)(struct ks_timerset *set);
};

void ks_timerset_init(
	struct ks_timerset *set,
	void (*timers_updated)(struct ks_timerset *set));

longtime_t ks_timerset_next(struct ks_timerset *set);
void ks_timerset_run(struct ks_timerset *set);

struct ks_timer
{
	struct list_head node;

	struct ks_timerset *set;

	const char *name;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(void *data);
};

void ks_timer_init(
	struct ks_timer *timer,
	struct ks_timerset *set,
	const char *name,
	void (*func)(void *data),
	void *data);

void ks_timer_start(
	struct ks_timer *timer,
	longtime_t expires);

void ks_timer_start_delta(
	struct ks_timer *timer,
	longtime_t delta);

void ks_timer_stop(struct ks_timer *timer);

static inline int ks_timer_pending(struct ks_timer *timer)
{
	return timer->pending;
}

#endif

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
#include <libkstreamer/util.h>

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

enum ks_timer_action
{
	KS_TIMER_STARTED,
	KS_TIMER_STOPPED,
//	KS_TIMER_RESTARTED,
	KS_TIMER_FIRED,
};

struct ks_timer
{
	struct list_head node;

	int refcnt;

	struct ks_timerset *set;

	const char *name;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(struct ks_timer *timer, enum ks_timer_action action, void *start_data);
};

struct ks_timer *ks_timer_create(
	struct ks_timer *timer,
	struct ks_timerset *set,
	const char *name,
	void (*func)(struct ks_timer *timer, enum ks_timer_action action, void *start_data));

KSBOOL ks_timer_start(
	struct ks_timer *timer,
	longtime_t expires,
	void *start_data);

KSBOOL ks_timer_start_delta(
	struct ks_timer *timer,
	longtime_t delta,
	void *start_data);

KSBOOL ks_timer_stop(struct ks_timer *timer);

static inline int ks_timer_pending(struct ks_timer *timer)
{
	return timer->pending;
}

struct ks_timer *ks_timer_get(struct ks_timer *req);
void ks_timer_put(struct ks_timer *req);

#endif

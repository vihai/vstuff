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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <libkstreamer/timer.h>
#include <libkstreamer/util.h>

#include <longtime.h>

void ks_timerset_init(
	struct ks_timerset *set,
	void (*timers_updated)(struct ks_timerset *set))
{
	memset(set, 0, sizeof(*set));

	INIT_LIST_HEAD(&set->timers);

	pthread_mutex_init(&set->timers_lock, NULL);

	set->timers_updated = timers_updated;
}

void ks_timer_init(
	struct ks_timer *timer,
	struct ks_timerset *set,
	const char *name,
	void (*func)(void *data),
	void *data)
{
	timer->set = set;
	timer->name = name;
	timer->expires = 0LL;
	timer->func = func;
	timer->data = data;
	timer->pending = FALSE;
}

static void _ks_timer_add(
	struct ks_timer *timer,
	longtime_t expires)
{
	struct ks_timerset *set = timer->set;

	assert(set);

	timer->expires = expires;

	pthread_mutex_lock(&set->timers_lock);
	if (list_empty(&set->timers) ||
	    list_entry(set->timers.prev, struct ks_timer, node)->expires <
							timer->expires) {

		list_add_tail(&timer->node, &set->timers);
		timer->pending = TRUE;
	} else {
		struct ks_timer *timert;
		list_for_each_entry(timert, &set->timers, node) {
			if (timert->expires > timer->expires) {
				list_add_tail(&timer->node, &timert->node);
				timer->pending = TRUE;
				break;
			}
		}
	}
	pthread_mutex_unlock(&set->timers_lock);

	assert(timer->pending);
}

void ks_timer_add(
	struct ks_timer *timer,
	longtime_t expires)
{
	assert(!timer->pending);

	_ks_timer_add(timer, expires);
}

void ks_timer_start(
	struct ks_timer *timer,
	longtime_t expires)
{
	struct ks_timerset *set = timer->set;

	if (timer->pending) {
		if (timer->expires == expires)
			return;

		pthread_mutex_lock(&set->timers_lock);
		list_del(&timer->node);
		pthread_mutex_unlock(&set->timers_lock);
		timer->pending = FALSE;
	}

	_ks_timer_add(timer, expires);

	if (set->timers_updated)
		set->timers_updated(set);
}

void ks_timer_stop(struct ks_timer *timer)
{
	if (timer->pending) {
		pthread_mutex_lock(&timer->set->timers_lock);
		list_del(&timer->node);
		pthread_mutex_unlock(&timer->set->timers_lock);

		timer->pending = FALSE;
	}
}

void ks_timer_start_delta(
	struct ks_timer *timer,
	longtime_t delta)
{
	ks_timer_start(timer, longtime_now() + delta);
}

void ks_timerset_run(struct ks_timerset *set)
{
	longtime_t now = longtime_now();

restart:;
	struct ks_timer *timer;
	pthread_mutex_lock(&set->timers_lock);
	list_for_each_entry(timer, &set->timers, node) {

		if (timer->expires < now) {
			list_del(&timer->node);
			timer->pending = FALSE;

			/* Restart from beginning, as func may have started
			 * stopped or changed timers
			 */

			pthread_mutex_unlock(&set->timers_lock);
			timer->func(timer->data);

			goto restart;
		} else
			break;
	}
	pthread_mutex_unlock(&set->timers_lock);
}

longtime_t ks_timerset_next(struct ks_timerset *set)
{
	longtime_t ret;

	pthread_mutex_lock(&set->timers_lock);
	if (list_empty(&set->timers))
		ret = -1;
	else
		ret = max(list_entry(set->timers.next, struct ks_timer, node)
					->expires - longtime_now(), 0LL);
	pthread_mutex_unlock(&set->timers_lock);

	return ret;
}

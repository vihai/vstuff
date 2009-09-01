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
#include <sys/time.h>

#include "timer.h"
#include "longtime.h"

void vgsm_timerset_init(
	struct vgsm_timerset *set,
	void (*timers_updated)(struct vgsm_timerset *set))
{
	memset(set, 0, sizeof(*set));

	INIT_LIST_HEAD(&set->timers);

	ast_mutex_init(&set->timers_lock);

	set->timers_updated = timers_updated;
}

struct vgsm_timer *vgsm_timer_create(
	struct vgsm_timer *timer,
	struct vgsm_timerset *set,
	const char *name,
	void (*func)(struct vgsm_timer *timer, enum vgsm_timer_action action, void *start_data))
{
	if (!timer) {
		timer = malloc(sizeof(struct vgsm_timer));
		if (!timer)
			return NULL;
	}

	memset(timer, 0, sizeof(*timer));

	timer->refcnt = 1;

	timer->set = set;
	timer->name = name;
	timer->expires = 0LL;
	timer->func = func;
	timer->data = NULL;
	timer->pending = FALSE;

	return timer;
}

static void _vgsm_timer_add(
	struct vgsm_timer *timer,
	longtime_t expires)
{
	struct vgsm_timerset *set = timer->set;

	assert(set);

	timer->expires = expires;

	if (list_empty(&set->timers) ||
	    list_entry(set->timers.prev, struct vgsm_timer, node)->expires <=
							timer->expires) {

		list_add_tail(&timer->node, &set->timers);
		timer->pending = TRUE;
	} else {
		struct vgsm_timer *timert;
		list_for_each_entry(timert, &set->timers, node) {
			if (timert->expires > timer->expires) {
				list_add_tail(&timer->node, &timert->node);
				timer->pending = TRUE;
				break;
			}
		}
	}

	assert(timer->pending);
}

void vgsm_timer_add(
	struct vgsm_timer *timer,
	longtime_t expires)
{
	ast_mutex_lock(&timer->set->timers_lock);

	assert(!timer->pending);

	_vgsm_timer_add(timer, expires);

	ast_mutex_unlock(&timer->set->timers_lock);
}

BOOL vgsm_timer_start(
	struct vgsm_timer *timer,
	longtime_t expires,
	void *start_data)
{
	struct vgsm_timerset *set = timer->set;

	ast_mutex_lock(&set->timers_lock);
	BOOL was_scheduled = timer->pending;

	if (timer->pending) {
		if (timer->expires == expires) {
			ast_mutex_unlock(&set->timers_lock);
			return FALSE;
		}

		timer->func(timer, VGSM_TIMER_STOPPED, NULL);

		list_del(&timer->node);
		timer->pending = FALSE;
	}

	timer->func(timer, VGSM_TIMER_STARTED, start_data);
	_vgsm_timer_add(timer, expires);

	ast_mutex_unlock(&set->timers_lock);

	if (set->timers_updated)
		set->timers_updated(set);

	return was_scheduled;
}

BOOL vgsm_timer_stop(struct vgsm_timer *timer)
{
	ast_mutex_lock(&timer->set->timers_lock);

	BOOL was_scheduled = timer->pending;
	if (timer->pending) {
		list_del(&timer->node);
		timer->pending = FALSE;

		timer->func(timer, VGSM_TIMER_STOPPED, NULL);
	}
	ast_mutex_unlock(&timer->set->timers_lock);

	return was_scheduled;
}

BOOL vgsm_timer_start_delta(
	struct vgsm_timer *timer,
	longtime_t delta,
	void *start_data)
{
	return vgsm_timer_start(timer, longtime_now() + delta, start_data);
}

void vgsm_timerset_run(struct vgsm_timerset *set)
{
	longtime_t now = longtime_now();

restart:;
	struct vgsm_timer *timer;
	ast_mutex_lock(&set->timers_lock);
	list_for_each_entry(timer, &set->timers, node) {

		if (timer->expires < now) {
			list_del(&timer->node);
			timer->pending = FALSE;

			/* Restart from beginning, as func may have started
			 * stopped or changed timers
			 */

			ast_mutex_unlock(&set->timers_lock);
			timer->func(timer, VGSM_TIMER_FIRED, NULL);

			goto restart;
		} else
			break;
	}
	ast_mutex_unlock(&set->timers_lock);
}

longtime_t vgsm_timerset_next(struct vgsm_timerset *set)
{
	longtime_t ret;

	ast_mutex_lock(&set->timers_lock);
	if (list_empty(&set->timers))
		ret = -1;
	else
		ret = max(list_entry(set->timers.next, struct vgsm_timer, node)
					->expires - longtime_now(), 0LL);
	ast_mutex_unlock(&set->timers_lock);

	return ret;
}

struct vgsm_timer *vgsm_timer_get(struct vgsm_timer *timer)
{
	assert(timer->refcnt > 0);

	if (timer)
		timer->refcnt++;

	return timer;
}

void vgsm_timer_put(struct vgsm_timer *timer)
{
	assert(timer->refcnt > 0);

	timer->refcnt--;

	if (!timer->refcnt) {
		free(timer);
	}
}


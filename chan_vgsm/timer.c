/*
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
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

#include <asterisk/lock.h>

#include "timer.h"
#include "util.h"

void vgsm_timerset_init(
	struct vgsm_timerset *set,
	void (*timers_updated)(struct vgsm_timerset *set))
{
	memset(set, 0, sizeof(*set));

	INIT_LIST_HEAD(&set->timers);

	ast_mutex_init(&set->timers_lock);

	set->timers_updated = timers_updated;
}

void vgsm_timer_init(
	struct vgsm_timer *timer,
	struct vgsm_timerset *set,
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

static void _vgsm_timer_add(
	struct vgsm_timer *timer,
	longtime_t expires)
{
	struct vgsm_timerset *set = timer->set;

	assert(set);

	timer->expires = expires;

	ast_mutex_lock(&set->timers_lock);
	if (list_empty(&set->timers) ||
	    list_entry(set->timers.prev, struct vgsm_timer, node)->expires <
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
	ast_mutex_unlock(&set->timers_lock);

	assert(timer->pending);
}

void vgsm_timer_add(
	struct vgsm_timer *timer,
	longtime_t expires)
{
	assert(!timer->pending);

	_vgsm_timer_add(timer, expires);
}

void vgsm_timer_start(
	struct vgsm_timer *timer,
	longtime_t expires)
{
	struct vgsm_timerset *set = timer->set;

	if (timer->pending) {
		if (timer->expires == expires)
			return;

		ast_mutex_lock(&set->timers_lock);
		list_del(&timer->node);
		ast_mutex_unlock(&set->timers_lock);
		timer->pending = FALSE;
	}

	_vgsm_timer_add(timer, expires);

	if (set->timers_updated)
		set->timers_updated(set);
}

void vgsm_timer_stop(struct vgsm_timer *timer)
{
	if (timer->pending) {
		ast_mutex_lock(&timer->set->timers_lock);
		list_del(&timer->node);
		ast_mutex_unlock(&timer->set->timers_lock);

		timer->pending = FALSE;
	}
}

void vgsm_timer_start_delta(
	struct vgsm_timer *timer,
	longtime_t delta)
{
	vgsm_timer_start(timer, longtime_now() + delta);
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
			timer->func(timer->data);

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

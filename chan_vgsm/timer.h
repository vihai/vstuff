/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_TIMER_H
#define _VGSM_TIMER_H

#include <list.h>

#include "longtime.h"

struct vgsm_timerset
{
	struct list_head timers;
	ast_mutex_t timers_lock;

	void *data;

	void (*timers_updated)(struct vgsm_timerset *set);
};

void vgsm_timerset_init(
	struct vgsm_timerset *set,
	void (*timers_updated)(struct vgsm_timerset *set));

longtime_t vgsm_timerset_next(struct vgsm_timerset *set);
void vgsm_timerset_run(struct vgsm_timerset *set);

struct vgsm_timer
{
	struct list_head node;

	struct vgsm_timerset *set;

	const char *name;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(void *data);
};

void vgsm_timer_init(
	struct vgsm_timer *timer,
	struct vgsm_timerset *set,
	const char *name,
	void (*func)(void *data),
	void *data);

void vgsm_timer_start(
	struct vgsm_timer *timer,
	longtime_t expires);

void vgsm_timer_start_delta(
	struct vgsm_timer *timer,
	longtime_t delta);

void vgsm_timer_stop(struct vgsm_timer *timer);

static inline int vgsm_timer_pending(struct vgsm_timer *timer)
{
	return timer->pending;
}

#endif

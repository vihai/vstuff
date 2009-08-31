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

#ifndef _VGSM_TIMER_H
#define _VGSM_TIMER_H

#include <asterisk/lock.h>

#include <list.h>
#include <longtime.h>
#include <util.h>

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

enum vgsm_timer_action
{
	VGSM_TIMER_STARTED,
	VGSM_TIMER_STOPPED,
//	VGSM_TIMER_RESTARTED,
	VGSM_TIMER_FIRED,
};

struct vgsm_timer
{
	struct list_head node;

	int refcnt;

	struct vgsm_timerset *set;

	const char *name;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(struct vgsm_timer *timer, enum vgsm_timer_action action, void *start_data);
};

struct vgsm_timer *vgsm_timer_create(
	struct vgsm_timer *timer,
	struct vgsm_timerset *set,
	const char *name,
	void (*func)(struct vgsm_timer *timer, enum vgsm_timer_action action, void *start_data));

BOOL vgsm_timer_start(
	struct vgsm_timer *timer,
	longtime_t expires,
	void *start_data);

BOOL vgsm_timer_start_delta(
	struct vgsm_timer *timer,
	longtime_t delta,
	void *start_data);

BOOL vgsm_timer_stop(struct vgsm_timer *timer);

static inline int vgsm_timer_pending(struct vgsm_timer *timer)
{
	return timer->pending;
}

struct vgsm_timer *vgsm_timer_get(struct vgsm_timer *req);
void vgsm_timer_put(struct vgsm_timer *req);

#endif

#ifndef _Q931_TIMER_H
#define _Q931_TIMER_H

#include "list.h"

typedef long long longtime_t;

struct q931_timer
{
	struct list_head node;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(void *data);
};


static inline void q931_init_timer(
	struct q931_timer *timer)
{
	timer->func = NULL;
	timer->expires = 0LL;
	timer->data = NULL;
	timer->pending = 0;
}

static inline void q931_start_timer(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t expires)
{
	timer->expires = expires;

	if (!timer->pending) {
		list_add_tail(&timer->node, &lib->timers);

		timer->pending = TRUE;
	}
}

static inline lingtime_t longtime_now()
{
	struct timeval_t now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

static inline void q931_start_timer_delta(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t delta)
{
	q931_start_timer(lib, timer, longtime_now() + delta);
}

static inline void q931_stop_timer(struct q931_timer *timer)
{
	if (timer->pending) {
		list_del(&timer->node);

		timer->pending = FALSE;
	}
}

static inline int q931_timer_pending(struct q931_timer *timer)
{
	return timer->pending;
}

extern longtime_t q931_run_timers(struct q931_lib *lib);

#endif

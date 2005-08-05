#ifndef _LIBQ931_TIMER_H
#define _LIBQ931_TIMER_H

#include "list.h"

typedef long long longtime_t;

struct q931_lib;

struct q931_timer
{
	struct list_head node;

	int pending;

	longtime_t expires;

	void *data;
	void (*func)(void *data);
};

extern longtime_t q931_run_timers(struct q931_lib *lib);

#ifdef Q931_PRIVATE

extern void q931_init_timer(
	struct q931_timer *timer,
	void (*func)(void *data),
	void *data);

extern void q931_start_timer(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t expires);

extern longtime_t longtime_now();

extern void q931_start_timer_delta(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t delta);

extern void q931_stop_timer(struct q931_timer *timer);

static inline int q931_timer_pending(struct q931_timer *timer)
{
	return timer->pending;
}

#endif

#endif

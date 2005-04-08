#include <string.h>
#include <sys/time.h>

#define Q931_PRIVATE

#include "list.h"
#include "q931.h"
#include "timer.h"

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

longtime_t q931_longtime_now()
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

void q931_start_timer_delta(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t delta)
{
	q931_start_timer(lib, timer, q931_longtime_now() + delta);
}

void q931_stop_timer(struct q931_timer *timer)
{
	if (timer->pending) {
		list_del(&timer->node);

		timer->pending = FALSE;
	}
}

longtime_t q931_run_timers(struct q931_lib *lib)
{
	longtime_t now = q931_longtime_now();
	longtime_t next_timer = -1;
	int next_timer_set = 0;

	struct q931_timer *timer, *t;
	list_for_each_entry_safe(timer, t, &lib->timers, node) {
		if (!next_timer_set || timer->expires < next_timer)
			next_timer = timer->expires;

		if (timer->expires < now) {
			timer->func(timer->data);

			list_del(&timer->node);

			timer->pending = FALSE;
		}
	}

	if (next_timer == -1)
		return -1;
	else
		return next_timer - now;
}


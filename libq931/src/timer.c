#include <stdio.h>
//#include <unistd.h>
//#include <stdlib.h>
//#include <errno.h>
//#include <string.h>
//#include <sys/time.h>
//#include <sys/socket.h>
//#include <sys/ioctl.h>
//#include <linux/types.h>
//#include <assert.h>
//#include <stdarg.h>
//#include <fcntl.h>

#include "list.h"
#include "q931.h"
#include "timer.h"

void q931_init_timer(
	struct q931_timer *timer)
{
	timer->func = NULL;
	timer->expires = 0LL;
	timer->data = NULL;
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

lingtime_t longtime_now()
{
	struct timeval_t now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

void q931_start_timer_delta(
	struct q931_lib *lib,
	struct q931_timer *timer,
	longtime_t delta)
{
	q931_start_timer(lib, timer, longtime_now() + delta);
}

void q931_stop_timer(struct q931_timer *timer)
{
	if (timer->pending) {
		list_del(&timer->node);

		timer->pending = FALSE;
	}
}

int q931_timer_pending(struct q931_timer *timer)
{
longtime_t q931_run_timers(struct q931_lib *lib)
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	longtime_t now = now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
	longtime_t next_timer = -1;
	int next_timer_set = 0;

	struct q931_timer *timer;
	list_for_each_entry(timer, &lib->timers, node) {
		if (!next_timer_set || timer->expires < next_timer)
			next_timer = timer->expires;

		if (timer->expires < now) {
			timer->func(timer->data);

			list_del(&timer->node);

			timer->pending = FALSE;
		}
	}

	return next_timer;
}


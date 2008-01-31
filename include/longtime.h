/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LONGTIME_H
#define _LONGTIME_H

#include <sys/time.h>

#define SEC 1000000LL
#define MILLISEC 1000LL

typedef long long longtime_t;

static inline longtime_t longtime_from(struct timeval *tv)
{
	return tv->tv_sec * 1000000LL + tv->tv_usec;
}

static inline longtime_t longtime_now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return longtime_from(&tv);
}

#endif

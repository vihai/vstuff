/*
 * vISDN - Controlling program
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSMCTL_H
#define _VGSMCTL_H

#include <list.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE !TRUE
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct module
{
	struct list_head node;

	const char *cmd;

	int (*do_it)(const char *device, int argc, char *argv[], int optind);
	void (*usage)(int argc, char *argv[]);
};

#define verbose(format, arg...)						\
	if (verbosity)							\
		fprintf(stderr,						\
			format,						\
			## arg)

extern int verbosity;

extern void print_usage(const char *fmt, ...);

#endif

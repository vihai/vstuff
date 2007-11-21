/*
 * kstreamer's controlling program
 *
 * Copyright (C) 2005-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KSTOOL_H
#define _KSTOOL_H

#include <list.h>

typedef unsigned char BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE !TRUE
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(a,b) ((a) > (b) ? (b) : (a))

struct global_state
{
	int argc;
	char **argv;

	int verbosity;

	struct ks_feature *hdlc_framer;
	struct ks_feature *hdlc_deframer;
	struct ks_feature *octet_reverser;

	struct list_head modules;

	struct ks_conn *conn;

};

extern struct global_state glob;

struct module
{
	struct list_head node;

	const char *cmd;

	int (*do_it)(int optind);
	void (*usage)(void);
};

#define verbose(format, arg...)		\
	if (glob.verbosity)		\
		fprintf(stderr,		\
			format,		\
			## arg)

extern void print_usage(const char *fmt, ...);

#endif

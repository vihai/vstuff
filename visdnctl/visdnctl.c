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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <linux/types.h>
#include <assert.h>
#include <limits.h>
#include <libgen.h>

#include <getopt.h>

#include <linux/visdn/cxc.h>

#include "visdnctl.h"
#include "connect.h"
#include "disconnect.h"
#include "enable_path.h"
#include "disable_path.h"
#include "netdev.h"

int global_argc;
char **global_argv;

int verbosity = 0;
struct list_head modules = LIST_HEAD_INIT(modules);

void print_usage(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	fprintf(stderr,
		"%s: [options] <command> [parameters]\n"
		"\n"
		"Options:"
		"    -v --verbose: Increase verbosity level\n"
		"\n"
		"Commands:\n\n",
		global_argv[0]);

	struct module *module;
	list_for_each_entry(module, &modules, node) {
		printf("\n");

		if (module->usage)
			module->usage(global_argc, global_argv);
	}

	if (fmt) {
		fprintf(stderr, "\n\n=========> ");
		vfprintf(stderr, fmt, ap);
	}

	exit(1);
}

int decode_chan_id(const char *chan_str)
{
	int chan_id;

	if (chan_str[0] == '/') {
		char real_path[PATH_MAX];

		if (!realpath(chan_str, real_path)) {
			fprintf(stderr, "Cannot resolve path '%s': %s\n",
				chan_str, strerror(errno));
			return 1;
		}

		chan_id = atoi(basename(real_path));

		if (!chan_id) {
			fprintf(stderr, "Cannot get id from '%s': %s\n",
				real_path, strerror(errno));
			return 1;
		}
	} else {
		chan_id = atoi(chan_str);
	}

	return chan_id;
}

int main(int argc, char *argv[])
{
	const char *command = NULL;
	int c;
	int optidx;

	global_argc = argc;
	global_argv = argv;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	list_add_tail(&module_connect.node, &modules);
	list_add_tail(&module_disconnect.node, &modules);
	list_add_tail(&module_enable_path.node, &modules);
	list_add_tail(&module_disable_path.node, &modules);
	list_add_tail(&module_netdev.node, &modules);

	struct option options[] = {
		{ "verbose", no_argument, 0, 0 },
		{ }
	};

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	for(;;) {
		c = getopt_long(argc, argv, "", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 'v' || (c == 0 &&
		    !strcmp(options[optidx].name, "verbose"))) {
			verbosity++;
		} else {
			if (c) {
				print_usage("Unknow option '%c'\n", c);
			} else {
				print_usage("Unknow option %s\n",
					options[optidx].name);
			}
		}
	}

	if (argc <= optind) {
		print_usage("Missing required command\n");
	}

	command = argv[optind];

	struct module *module;
	list_for_each_entry(module, &modules, node) {
		if (!strcasecmp(command, module->cmd) &&
		    module->do_it) {
			return module->do_it(argc, argv, optind);
		}
	}

	print_usage("Unknown command '%s'\n", command);

	return 1;
}

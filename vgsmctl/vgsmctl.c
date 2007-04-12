/*
 * vGSM - Controlling program
 *
 * Copyright (C) 2005-2007 Daniele Orlandi
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

#include "vgsmctl.h"
#include "codec.h"
#include "power.h"
#include "pad_timeout.h"
#include "fw_version.h"
#include "fw_upgrade.h"

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
		"    -d --device: Device\n"
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

int main(int argc, char *argv[])
{
	const char *command = NULL;
	int c;
	int optidx;

	global_argc = argc;
	global_argv = argv;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	list_add_tail(&module_codec.node, &modules);
	list_add_tail(&module_power.node, &modules);
	list_add_tail(&module_pad_timeout.node, &modules);
	list_add_tail(&module_fw_version.node, &modules);
	list_add_tail(&module_fw_upgrade.node, &modules);

	struct option options[] = {
		{ "verbose", no_argument, 0, 0 },
		{ "device", required_argument, 0, 0 },
		{ }
	};

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	const char *device = NULL;

	for(;;) {
		c = getopt_long(argc, argv, "vd:", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 'v' || (c == 0 &&
		    !strcmp(options[optidx].name, "verbose"))) {
			verbosity++;
		} else if (c == 'd' || (c == 0 &&
		    !strcmp(options[optidx].name, "device"))) {
			device = optarg;
		} else {
			if (c) {
				print_usage("Unknow option '%c'\n",
					c);
			} else {
				print_usage("Unknow option %s\n",
					options[optidx].name);
			}
		}
	}

	if (argc <= optind) {
		print_usage("Missing required command\n");
	}

	if (!device) {
		print_usage("Missing module option\n");
	}

	command = argv[optind];

	struct module *module;
	list_for_each_entry(module, &modules, node) {
		if (!strcasecmp(command, module->cmd) &&
		    module->do_it) {
			return module->do_it(device, argc, argv, optind);
		}
	}

	print_usage("Unknown command '%s'\n", command);

	return 1;
}

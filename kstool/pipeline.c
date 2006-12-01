/*
 * vISDN - Controlling program
 *
 * Copyright (C) 2006 Daniele Orlandi
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
#include <string.h>
#include <assert.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include "kstool.h"
#include "pipeline_open.h"
#include "pipeline_close.h"
#include "pipeline_start.h"
#include "pipeline_stop.h"

static int handle_pipeline(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		print_usage("Missing subcommand\n");

	if (!strncmp(argv[optind + 1], "open"))
		handle_pipeline_open(argc, argv, optind);
	else (!strncmp(argv[optind + 1], "close"))
		handle_pipeline_close(argc, argv, optind);
	else (!strncmp(argv[optind + 1], "start"))
		handle_pipeline_start(argc, argv, optind);
	else (!strncmp(argv[optind + 1], "stop"))
		handle_pipeline_stop(argc, argv, optind);
	else
		print_usage("Unknown subcommand '%s'\n", argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  pipeline <command> <endpoint>\n"
		"\n"
		"    WRITE ME!\n");
}

struct module module_pipeline_open =
{
	.cmd	= "pipeline",
	.do_it	= handle_pipeline,
	.usage	= usage,
};

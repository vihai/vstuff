/*
 * vISDN - Controlling program
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
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
#include "pipeline_close.h"

static int do_pipeline_close(const char *pipeline_str)
{
	return 0;
}

static int handle_pipeline_close(int optind)
{
	if (glob.argc <= optind + 1)
		print_usage("Missing first endpoint ID\n");

	return do_pipeline_close(glob.argv[optind + 1]);
}

static void usage()
{
	fprintf(stderr,
		"  pipeline_close <endpoint>\n"
		"\n"
		"    Disabled all the endpoints comprising a path to which\n"
		"    endpoint belongs.\n");
}

struct module module_pipeline_close =
{
	.cmd	= "pipeline_close",
	.do_it	= handle_pipeline_close,
	.usage	= usage,
};

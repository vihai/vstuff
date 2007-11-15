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

#include <libkstreamer.h>

#include "kstool.h"
#include "connect.h"

int disconnect_pipeline(const char *pipeline_str)
{
	struct ks_pipeline *pipeline;
	int err;

	pipeline = ks_pipeline_get_by_string(glob.conn, pipeline_str);
	if (!pipeline) {
		fprintf(stderr, "Cannot find pipeline '%s'\n", pipeline_str);
		return 1;
	}

	err = ks_pipeline_destroy(pipeline, glob.conn);
	if (err < 0) {
		fprintf(stderr, "Cannot destroy the pipeline\n");
		return 1;
	}

	return 0;
}

int disconnect_endpoint(const char *endpoint_id_str)
{
	return 0;
}

int handle_disconnect(int optind)
{
	if (glob.argc <= optind + 1)
		print_usage("Missing disconnection type\n");

	if (glob.argc <= optind + 2)
		print_usage("Missing disconnection parameter\n");

	if (!strcasecmp(glob.argv[optind + 1], "pipeline"))
		disconnect_pipeline(glob.argv[optind + 2]);
	else if (!strcasecmp(glob.argv[optind + 1], "endpoint"))
		disconnect_endpoint(glob.argv[optind + 2]);
	else
		print_usage("Invalid disconnection type\n");

	return 0;
}

static void usage()
{
	fprintf(stderr,
		"  disconnect <pipeline|endpoint> <id>\n"
		"\n");
}

struct module module_disconnect =
{
	.cmd	= "disconnect",
	.do_it	= handle_disconnect,
	.usage	= usage,
};

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

#include <libkstreamer.h>

#include "kstool.h"
#include "pipeline_close.h"

static int do_pipeline_close(const char *pipeline_str)
{
	struct ks_pipeline *pipeline;
	int err;

	pipeline = ks_pipeline_get_by_string(glob.conn, pipeline_str);
	if (!pipeline) {
		fprintf(stderr, "Cannot find pipeline '%s'\n", pipeline_str);
		return 1;
	}

	if (pipeline->status != KS_PIPELINE_STATUS_OPEN) {
		ks_pipeline_put(pipeline);
		fprintf(stderr, "Pipeline is not ready\n");
		return 1;
	}

	pipeline->status = KS_PIPELINE_STATUS_CONNECTED;

	err = ks_pipeline_update(pipeline, glob.conn);
	if (err < 0) {
		fprintf(stderr, "Cannot update the pipeline\n");
		return 1;
	}

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

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

#include <linux/visdn/softcxc.h>
#include <linux/visdn/cxc.h>
#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "pipeline_stop.h"

static int do_pipeline_stop(const char *chan_str)
{
	int chan_id = decode_chan_id(chan_str);

	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;
	connect.src_chan_id = chan_id;
	connect.dst_chan_id = 0;
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_PIPELINE_STOP, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_PIPELINE_STOP) failed: %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	return 0;
}

static int handle_pipeline_stop(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		print_usage("Missing first channel ID\n");

	return do_pipeline_stop(argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  pipeline_stop <chan>\n"
		"\n"
		"    Disabled all the channels comprising a path to which\n"
		"    chan belongs.\n");
}

struct module module_pipeline_stop =
{
	.cmd	= "pipeline_stop",
	.do_it	= handle_pipeline_stop,
	.usage	= usage,
};

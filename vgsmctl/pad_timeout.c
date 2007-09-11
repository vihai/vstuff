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
#include <string.h>
#include <assert.h>
#include <libgen.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <list.h>

#include <linux/vgsm.h>

#include "vgsmctl.h"
#include "pad_timeout.h"

static int do_pad_timeout(
	const char *device,
	const char *value)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	if (ioctl(fd, VGSM_IOC_PAD_TIMEOUT, atoi(value)) < 0) {
		fprintf(stderr, "ioctl(IOC_PAD_TIMEOUT) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_pad_timeout(const char *module, int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing <value>\n");
	}

	return do_pad_timeout(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  pad_timeout <value>\n"
		"\n"
		"    Set serial port padding timeout to value <value>:\n");
}

struct module module_pad_timeout =
{
	.cmd	= "pad_timeout",
	.do_it	= handle_pad_timeout,
	.usage	= usage,
};

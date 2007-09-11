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
#include "fw_version.h"

static int do_fw_version(
	const char *device,
	const char *value)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	int val = 0;

	if (ioctl(fd, VGSM_IOC_FW_VERSION, &val) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_VERSION) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_fw_version(const char *module, int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing <value>\n");
	}

	return do_fw_version(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  fw_version\n"
		"\n"
		"   Gets the firmware version of the uC corresponding to the\n"
		"   specified module\n");
}

struct module module_fw_version =
{
	.cmd	= "fw_version",
	.do_it	= handle_fw_version,
	.usage	= usage,
};

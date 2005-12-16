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
#include "power.h"

static int do_power(
	const char *device,
	const char *value)
{
	int fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	int val;
	if (!strcasecmp(value, "toggle")) {
		val = 0;
	} else if (!strcasecmp(value, "uncond_off")) {
		val = 1;
	} else {
		fprintf(stderr, "Unknown value '%s'\n", value);
		return 1;
	}

	if (ioctl(fd, VGSM_IOC_POWER, val) < 0) {
		fprintf(stderr, "ioctl(IOC_POWER) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_power(const char *module, int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing <value>\n");
	}

	return do_power(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  power <value>\n"
		"\n"
		"    value can be:\n"
		"    toggle: toggle module on/off)\n"
		"    uncond_off: immediately turn module off)\n");
}

struct module module_power =
{
	.cmd	= "power",
	.do_it	= handle_power,
	.usage	= usage,
};

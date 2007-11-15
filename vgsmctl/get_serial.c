/*
 * vGSM's controlling program
 *
 * Copyright (C) 2007 Daniele Orlandi
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
#include <linux/vgsm2.h>

#include "vgsmctl.h"
#include "get_serial.h"

static int do_get_serial(
	const char *device,
	const char *value)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	__u32 serial;

	if (ioctl(fd, VGSM_IOC_READ_SERIAL, &serial) < 0) {
		fprintf(stderr, "ioctl(IOC_READ_SERIAL) failed: %s\n",
			strerror(errno));

		return 1;
	}

	printf("%012d\n", serial);

	return 0;
}

static int handle_get_serial(
	const char *module,
	int argc, char *argv[], int optind)
{
	return do_get_serial(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  get_serial\n"
		"\n"
		"   Print the card's serial number\n");
}

struct module module_get_serial =
{
	.cmd	= "get_serial",
	.do_it	= handle_get_serial,
	.usage	= usage,
};

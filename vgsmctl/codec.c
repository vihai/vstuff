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
#include "codec.h"

static int do_codec(
	const char *device,
	const char *parameter,
	const char *value)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct vgsm_codec_ctl cctl;

	if (!strcasecmp(parameter, "reset")) {
		cctl.parameter = VGSM_CODEC_RESET;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "rxgain")) {
		cctl.parameter = VGSM_CODEC_RXGAIN;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "txgain")) {
		cctl.parameter = VGSM_CODEC_TXGAIN;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "dig_loop")) {
		cctl.parameter = VGSM_CODEC_DIG_LOOP;
		cctl.value = atoi(value);
	} else if (!strcasecmp(parameter, "anal_loop")) {
		cctl.parameter = VGSM_CODEC_ANAL_LOOP;
		cctl.value = atoi(value);
	} else {
		fprintf(stderr, "Unknown parameter '%s'\n", parameter);
		return 1;
	}

	if (ioctl(fd, VGSM_IOC_CODEC_SET, &cctl) < 0) {
		fprintf(stderr, "ioctl(IOC_CODEC_SET) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_codec(const char *module, int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing <parameter>\n");
	}

	if (argc <= optind + 2) {
		print_usage("Missing <value>\n");
	}

	return do_codec(module, argv[optind + 1], argv[optind + 2]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  codec <parameter> <value>\n"
		"\n"
		"    Available parameters are:\n"
		"    rxgain: RX-gain (0-255)\n"
		"    txgain: TX gain (0-255)\n"
		"    dig_loop: Digital loop (enabled=1/disabled=0)\n"
		"    anal_loop: Analog loop (enabled=1/disabled=0)\n");
}

struct module module_codec =
{
	.cmd	= "codec",
	.do_it	= handle_codec,
	.usage	= usage,
};

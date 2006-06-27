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
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <list.h>

#include <linux/vgsm.h>

#include "vgsmctl.h"
#include "fw_upgrade.h"

static int do_fw_upgrade(
	const char *device,
	const char *filename)
{
	int fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n",
			device,
			strerror(errno));

		return 1;
	}

	int in_fd = open(filename, O_RDONLY);
	if (in_fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n",
			filename,
			strerror(errno));

		return 1;
	}

	struct stat stat;
	if (fstat(in_fd, &stat) < 0) {
		fprintf(stderr, "stat failed: %s\n",
			strerror(errno));

		return 1;
	}

	printf("Firmware size: %d bytes\n", (int)stat.st_size);

	struct vgsm_fw_header *fwb;
	fwb = malloc(sizeof(*fwb) + stat.st_size);
	if (!fwb) {
		fprintf(stderr, "malloc failed\n");
		return 1;
	}

	fwb->size = stat.st_size;

	int nread = read(in_fd, fwb->data, fwb->size);
	if (nread < 0) {
		fprintf(stderr, "Cannot read '%s': %s\n",
			filename,
			strerror(errno));
		return 1;
	} else if (nread < fwb->size) {
		fprintf(stderr, "Short read on '%s': %s\n",
			filename,
			strerror(errno));
		return 1;
	}

	int i;
	for (i=0; i<fwb->size; i++) {
		fwb->checksum += fwb->data[i];
	}

	fwb->checksum &= 0x00ff;

	printf("Checksum: 0x%04x\n", fwb->checksum);

	if (ioctl(fd, VGSM_IOC_FW_UPGRADE, fwb) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_UPGRADE) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_fw_upgrade(
	const char *module, int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing <filename>\n");
	}

	return do_fw_upgrade(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  fw_upgrade\n"
		"\n"
		"   Updates the firmware of the uC corresponding to the\n"
		"   specified module\n");
}

struct module module_fw_upgrade =
{
	.cmd	= "fw_upgrade",
	.do_it	= handle_fw_upgrade,
	.usage	= usage,
};

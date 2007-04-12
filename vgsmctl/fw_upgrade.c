/*
 * vISDN - Controlling program
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
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <linux/types.h>

#include <list.h>

#include <linux/vgsm.h>
#include <linux/vgsm2.h>

#include <zlib.h>

#include "vgsmctl.h"
#include "fw_upgrade.h"

struct fw_file_header
{
	__u32 magic;
	__u32 crc;
	__u32 size;
	__u8 version[3];
	__u8 reserved[14];
};

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

	struct fw_file_header ffh;
	int nread = read(in_fd, &ffh, sizeof(ffh));
	if (nread < 0) {
		fprintf(stderr, "read failed: %s\n",
			strerror(errno));

		return 1;
	} else if (nread < sizeof(ffh)) {
		fprintf(stderr, "short read in '%s'\n",
			filename);

		return 1;
	}

	int interface_version;
	if (ioctl(fd, VGSM_IOC_GET_INTERFACE_VERSION,
		&interface_version) < 0) {
		interface_version = 1;
	}

	if (interface_version != 1 &&
	    interface_version != 2) {
		fprintf(stderr, "Unsupported interface version %d\n",
			interface_version);
		return 1;
	}

	__u32 expected_magic = 0x92e3299a;
	if (ntohl(ffh.magic) != expected_magic) {
		fprintf(stderr,
			"Firmware '%s' has invalid magic (0x%08x != 0x%08x)\n",
			filename,
			ntohl(ffh.magic),
			expected_magic);

		return 1;
	}

	if (ntohl(ffh.size) != stat.st_size) {
		fprintf(stderr, "Firmware '%s' has invalid size (%d != %d)\n",
			filename,
			ntohl(ffh.size),
			(int)stat.st_size);

		return 1;
	}

	struct vgsm_fw_header *fwb;
	fwb = malloc(sizeof(*fwb) + stat.st_size - sizeof(ffh));
	if (!fwb) {
		fprintf(stderr, "malloc failed\n");
		return 1;
	}

	fwb->size = stat.st_size - sizeof(ffh);

	printf("Firmware size: %d bytes\n", (int)stat.st_size);

	nread = read(in_fd, fwb->data, fwb->size);
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

	__u32 crc = crc32(0, fwb->data, fwb->size);

	printf("CRC: 0x%08x\n", crc);

	if (ntohl(ffh.crc) != crc) {
		fprintf(stderr,
			"Firmware '%s' has invalid CRC"
			" (0x%08x != 0x%08x)\n",
			filename,
			ntohl(ffh.crc),
			crc);

		return 1;
	}

	int i;
	for (i=0; i<fwb->size; i++) {
		fwb->checksum += fwb->data[i];
	}

	fwb->checksum &= 0x00ff;

	if (interface_version == 1) {
		printf("Checksum: 0x%04x\n", fwb->checksum);
	}

	__u32 cur_version;
	if (ioctl(fd, VGSM_IOC_FW_VERSION, &cur_version) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_VERSION) failed: %s\n",
			strerror(errno));

		return 1;
	}

	printf("Current version: %d.%d.%d\n",
		(cur_version & 0x00ff0000) >> 16,
		(cur_version & 0x0000ff00) >> 8,
		(cur_version & 0x000000ff) >> 0);

	printf("Version to program: %d.%d.%d\n",
		ffh.version[0],
		ffh.version[1],
		ffh.version[2]);

	printf("Updating firmware (this may take a while)...");

	if (ioctl(fd, VGSM_IOC_FW_UPGRADE, fwb) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_UPGRADE) failed: %s\n",
			strerror(errno));

		return 1;
	}

	printf("OK\n");

	if (interface_version == 2) {
		__u8 cmp[0x100000];

		printf("Comparing firmware (this may take a while)...");

		if (ioctl(fd, VGSM_IOC_FW_READ, cmp) < 0) {
			fprintf(stderr, "ioctl(IOC_FW_READ) failed: %s\n",
				strerror(errno));

			return 1;
		}

		int i;
		for(i=0; i<fwb->size; i++) {
			if (fwb->data[i] != cmp[i]) {
				fprintf(stderr,
					"Compare 0x%08x: 0x%08x != 0x%08x\n",
					i, cmp[i], fwb->data[i]);

				return 1;
			}
		}

		printf("OK\n");
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

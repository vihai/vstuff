/*
 * vGSM's controlling program
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
#include <sys/wait.h>

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

static const char *vgsm_upg_state_to_text(enum vgsm_fw_upgrade_state state)
{
	switch(state) {
	case VGSM_FW_UPGRADE_ERASE:
		return "ERASE";
	case VGSM_FW_UPGRADE_WRITE:
		return "WRITE";
	case VGSM_FW_UPGRADE_READ:
		return "READ";
	case VGSM_FW_UPGRADE_OK:
		return "OK";
	case VGSM_FW_UPGRADE_KO:
		return "KO";
	}

	return "*INVALID*";
}

static int spawn_monitor(int fd)
{
	int pid = fork();
	if (pid != 0)
		return pid;
	else if (pid < 0) {
		fprintf(stderr, "Cannot fork: %s\n", strerror(errno));
		return -1;
	}

	struct vgsm_fw_upgrade_stat upgstat;

	do {
		if (ioctl(fd, VGSM_IOC_FW_UPGRADE_STAT, &upgstat) < 0)
			_exit(1);

		printf("%s: 0x%05x/0x%05x          ",
			vgsm_upg_state_to_text(upgstat.state),
			upgstat.pos,
			upgstat.tot);

		usleep(50000);

		int i;
		for(i=0; i<40; i++)
			printf("\b");
	} while(upgstat.state != VGSM_FW_UPGRADE_OK ||
		upgstat.state != VGSM_FW_UPGRADE_KO);

	_exit(0);

	return 0;
}

static int do_fw_upgrade(
	const char *device,
	const char *filename)
{
	printf(
	"############################ WARNING ###############################\n"
	"  If firmware programming is interrupted by any means the flash\n"
	"  memory WILL be corrupted. If possible retry another programming\n"
	"  session, otherwise the hardware will need to be factory\n"
	"  reprogrammed with the related costs.\n"
	"\n"
	"  Please don't attempt firmware programming unless specifically\n"
	"  instructed.\n"
	"####################################################################\n"
	"\n");

	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
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

	struct vgsm_fw_version fw_version;
	if (ioctl(fd, VGSM_IOC_FW_VERSION, &fw_version) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_VERSION) failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct vgsm_fw_version fw_flash_version;
	if (ioctl(fd, VGSM_IOC_FW_FLASH_VERSION, &fw_flash_version) < 0) {
		fprintf(stderr, "ioctl(IOC_FW_FLASH_VERSION) failed: %s\n",
			strerror(errno));

		return 1;
	}

	printf("Current version running: %d.%d.%d\n",
		fw_version.maj,
		fw_version.min,
		fw_version.ser);

	if (fw_flash_version.maj != 0xff) {
		// Not yet programmed

		printf("Current version on flash: %d.%d.%d\n",
			fw_flash_version.maj,
			fw_flash_version.min,
			fw_flash_version.ser);
	}

	printf("Version to program: %d.%d.%d\n",
		ffh.version[0],
		ffh.version[1],
		ffh.version[2]);

	__u32 newver_code = (ffh.version[0] << 16) |
				(ffh.version[1] << 8) |
				(ffh.version[2] << 0);
	__u32 curver_code = (fw_version.maj << 16) |
				(fw_version.min << 8) |
				(fw_version.ser << 0);

	if (newver_code < curver_code)
		printf("WARNING: Firmware to program is older than running"
			" firmware!\n");

	if (newver_code == curver_code)
		printf("NOTICE: Firmware to program is the same version of"
			" running firmware.\n");

	if (isatty(1)) {
		while(TRUE) {
			printf("Are you sure? (Y/n): ");
			char answer[8];
			fgets(answer, sizeof(answer), stdin);

			if (answer[0] != 'Y' && answer[0] != 'y')
				return 1;

			if (answer[0] == 'Y' || answer[0] == 'y')
				break;
		}
	}

	printf("Updating firmware (this may take a while)...\n");

	sighandler_t orig_hup_handler = signal(SIGHUP, SIG_IGN);
	sighandler_t orig_int_handler = signal(SIGINT, SIG_IGN);
	sighandler_t orig_quit_handler = signal(SIGQUIT, SIG_IGN);
	sighandler_t orig_term_handler = signal(SIGTERM, SIG_IGN);

	int pid = 0;

	if (isatty(1))
		pid = spawn_monitor(fd);

	int err;
	err = ioctl(fd, VGSM_IOC_FW_UPGRADE, fwb);

	if (pid) {
		int status;
		int i;
		for(i=0; i<5; i++) {
			if (waitpid(pid, &status, WNOHANG) > 0)
				break;

			sleep(1);
		}

		if (i==5)
			kill(pid, SIGKILL);
	}

	printf("\n");

	if (err < 0) {
		fprintf(stderr, "ioctl(IOC_FW_UPGRADE) failed: %s\n",
			strerror(errno));
		goto err_fw_upgrade;
	}

	if (interface_version == 2) {
		__u8 cmp[0x100000];

		printf("Comparing firmware (this may take a while)...");

		if (ioctl(fd, VGSM_IOC_FW_READ, cmp) < 0) {
			fprintf(stderr, "ioctl(IOC_FW_READ) failed: %s\n",
				strerror(errno));
			goto err_fw_read;
		}

		int i;
		for(i=0; i<fwb->size; i++) {
			if (fwb->data[i] != cmp[i]) {
				fprintf(stderr,
					"Compare 0x%08x: 0x%08x != 0x%08x\n",
					i, cmp[i], fwb->data[i]);

				goto err_compare;
			}
		}

		printf("OK\n");
	}

	signal(SIGHUP, orig_hup_handler);
	signal(SIGINT, orig_int_handler);
	signal(SIGQUIT, orig_quit_handler);
	signal(SIGTERM, orig_term_handler);

	printf("\n\nFirmware has been SUCCESSFULLY programmed\n");
	printf("When possible 'rmmod vgsm2' to activate reconfiguration"
		" and reboot\n");

	return 0;

err_fw_upgrade:
err_fw_read:
err_compare:
	signal(SIGHUP, orig_hup_handler);
	signal(SIGINT, orig_int_handler);
	signal(SIGQUIT, orig_quit_handler);
	signal(SIGTERM, orig_term_handler);

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

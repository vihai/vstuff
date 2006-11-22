/*
 * Cologne Chip's HFC-4S / HFC-8S / HFC-E1 EEPROM programming tool
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/types.h>

static inline lo(__u16 val)
{
	return val & 0x00ff;
}

static inline hi(__u16 val)
{
	return val >> 8;
}

static void writeb(void *addr, __u8 val)
{
	*(volatile __u8 *)addr = val;
}

static __u8 readb(void *addr)
{
	return *(volatile __u8 *)addr;
}

enum access_mode
{
	ACCESS_BYTE,
	ACCESS_WORD,
	ACCESS_DWORD,
};

int main(int argc, char *argv[])
{
	int c;
	int optidx;
	enum access_mode access_mode;

	struct option options[] = {
		{ "--byte", no_argument, 0, 0 },
		{ "--word", no_argument, 0, 0 },
		{ "--dword", no_argument, 0, 0 },
		{ }
	};

	for(;;) {
		struct option no_opt ={ "", no_argument, 0, 0 };
		struct option *opt;

		c = getopt_long(argc, argv, "bwd", options, &optidx);

		if (c == -1)
			break;

		opt = c ? &no_opt : &options[optidx];

		if (c == 'b' || !strcmp(opt->name, "byte"))
			access = ACCESS_BYTE;
		else if (c == 'w' || !strcmp(opt->name, "word"))
			access = ACCESS_WORD;
		else if (c == 'd' || !strcmp(opt->name, "dword"))
			access = ACCESS_DWORD;
		} else {
			if (c) {
				fprintf(stderr, "Unknow option '%c'\n", c);
			} else {
				fprintf(stderr, "Unknow option %s\n",
					options[optidx].name);
			}

			return 1;
		}
	}

	if (argc == 1) {
		fprintf(stderr,
			"Usage: %s <base_addr> <\n",
			argv[0]);
		return 1;
	}

	if (argc < optind) {
		fprintf(stderr, "Base address not specified\n");
		return 1;
	}

	unsigned long base;
	if (sscanf(argv[optind], "%x", &base) < 1) {
		fprintf(stderr, "Invalid base address\n");
		return 1;
	}
	printf("Base address = 0x%08x\n", base);

	if (argc < optind + 1) {
		fprintf(stderr, "Command not specified\n");
		return 1;
	}

	if (!strchr(argv[optind + 1], '=')) {
		unsigned long reg;
		unsigned long value;

		sscanf(argv[optind + 1], "%lx=%lx", &reg, &value);

		write_memory(base, access_mode, reg, value);

	} else {
		unsigned long reg;

		sscanf(argv[optind + 1], "%lx", &reg);

		read_memory(base, access_mode, reg);
	}

	int memfd = open("/dev/mem", O_RDWR);
	if (memfd < 0) {
		fprintf(stderr, "Cannot open /dev/mem: %s\n", strerror(errno));
		return 1;
	}

	void *regs;
	regs = mmap(0, 65536, PROT_READ|PROT_WRITE, MAP_FIXED | MAP_SHARED,
			memfd, base);
	if (regs == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	__u8 chip_id = readb(regs + R_CHIP_ID) >> 4;
}

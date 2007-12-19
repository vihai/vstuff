/*
 * DSP tester
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/types.h>

#include "../modules/vgsm2/regs.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

struct vgsm2
{
	int memfd;

	void *regs;

	unsigned long regs_base;
};

static void writeb(struct vgsm2 *vgsm2, __u32 offs, __u8 val)
{
	*(volatile __u8 *)(vgsm2->regs + offs) = val;
}

static void writew(struct vgsm2 *vgsm2, __u32 offs, __u16 val)
{
	*(volatile __u16 *)(vgsm2->regs + offs) = val;
}

static void writel(struct vgsm2 *vgsm2, __u32 offs, __u32 val)
{
	*(volatile __u32 *)(vgsm2->regs + offs) = val;
}

static __u8 readb(struct vgsm2 *vgsm2, __u32 offs)
{
	return *(volatile __u8 *)(vgsm2->regs + offs);
}

static __u16 readw(struct vgsm2 *vgsm2, __u32 offs)
{
	return *(volatile __u16 *)(vgsm2->regs + offs);
}

static __u32 readl(struct vgsm2 *vgsm2, __u32 offs)
{
	return *(volatile __u32 *)(vgsm2->regs + offs);
}

static int map_memory(struct vgsm2 *vgsm2)
{
	vgsm2->memfd = open("/dev/mem", O_RDWR);
	if (vgsm2->memfd < 0) {
		fprintf(stderr, "Cannot open /dev/mem: %s\n", strerror(errno));
		return 1;
	}

	vgsm2->regs = mmap(0, 0x10000, PROT_READ|PROT_WRITE, MAP_SHARED,
			vgsm2->memfd, vgsm2->regs_base);
	if (vgsm2->regs == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

static void print_info(struct vgsm2 *vgsm2)
{
	writel(vgsm2, VGSM_R_ME_INT_ENABLE(3), 0);

	while(1)
	printf("ME_STATUS: 0x%08x\n", readl(vgsm2, VGSM_R_ME_STATUS(3)));
};

int main(int argc, char *argv[])
{
	int c;
	int optidx;
	struct vgsm2 vgsm2;

	struct option options[] = {
		{ "--byte", no_argument, 0, 0 },
		{ }
	};

	for(;;) {
		struct option no_opt ={ "", no_argument, 0, 0 };
		struct option *opt;

		c = getopt_long(argc, argv, "bwd", options, &optidx);

		if (c == -1)
			break;

		opt = c ? &no_opt : &options[optidx];

		if (c == 'b' || !strcmp(opt->name, "byte")) {
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
			"Usage: %s <regs_base>\n",
			argv[0]);
		return 1;
	}

	if (argc < optind + 1) {
		fprintf(stderr, "Base address not specified\n");
		return 1;
	}

	if (sscanf(argv[optind], "%lx", &vgsm2.regs_base) < 1) {
		fprintf(stderr, "Invalid base address\n");
		return 1;
	}
	printf("Registers base address = 0x%08lx\n", vgsm2.regs_base);

	if (argc < optind + 1) {
		fprintf(stderr, "Command not specified\n");
		return 1;
	}

#if 0
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
#endif

	if (map_memory(&vgsm2))
		return 1;

	print_info(&vgsm2);

	return 0;
}

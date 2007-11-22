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
#include "sim_route.h"

static int get_sim_route(const char *device)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	int sim_route;

	if (ioctl(fd, VGSM_IOC_GET_SIM_ROUTE, &sim_route) < 0) {
		fprintf(stderr, "ioctl(IOC_GET_SIM_ROUTE) failed: %s\n",
			strerror(errno));

		return 1;
	}

	if (sim_route == VGSM_SIM_ROUTE_EXTERNAL)
		printf("external\n");
	else
		printf("%d\n", sim_route);

	return 0;
}

static int do_sim_route(
	const char *device,
	const char *value)
{
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	int sim_route;
	if (!strcasecmp(value, "external"))
		sim_route = VGSM_SIM_ROUTE_EXTERNAL;
	else
		sim_route = atoi(value);

	if (ioctl(fd, VGSM_IOC_SET_SIM_ROUTE, sim_route) < 0) {
		fprintf(stderr, "ioctl(IOC_SET_SIM_ROUTE) failed: %s\n",
			strerror(errno));

		return 1;
	}

	return 0;
}

static int handle_sim_route(
	const char *module,
	int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		return get_sim_route(module);
	else
		return do_sim_route(module, argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  sim_route\n"
		"\n"
		"  Get/Set local SIM routing\n");
}

struct module module_sim_route =
{
	.cmd	= "sim_route",
	.do_it	= handle_sim_route,
	.usage	= usage,
};

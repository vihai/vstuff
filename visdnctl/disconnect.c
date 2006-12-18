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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

//#include <linux/visdn/softcxc.h>
//#include <linux/visdn/cxc.h>
#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "disconnect.h"

int disconnect_pipeline(int id)
{
	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;

	printf("Disconnecting pipeline '%06d'\n", id);
#if 0
	connect.pipeline_id = id;
	connect.from_endpoint_id = 0;
	connect.to_endpoint_id = 0;
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT, &connect) < 0) {
		fprintf(stderr,
			"ioctl(IOC_DISCONNECT) failed: %s\n",
			strerror(errno));

		return 1;
	}
#endif

	close(fd);

	return 0;
}

int disconnect_endpoint(const char *endpoint_id_str)
{
	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

#if 0
	struct visdn_connect connect;

	int endpoint_id = decode_endpoint_id(endpoint_id_str);

	printf("Disconnecting endpoint '%06d'\n", endpoint_id);

	connect.from_endpoint_id = endpoint_id;
	connect.to_endpoint_id = 0;
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT_ENDPOINT, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_DISCONNECT_ENDPOINT) failed: %s\n",
			strerror(errno));

		return 1;
	}
#endif
	close(fd);

	return 0;
}

int handle_disconnect(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		print_usage("Missing disconnection type\n");

	if (argc <= optind + 2)
		print_usage("Missing disconnection parameter\n");

	if (!strcasecmp(argv[optind + 1], "pipeline"))
		disconnect_pipeline(atoi(argv[optind + 2]));
	else if (!strcasecmp(argv[optind + 1], "endpoint"))
		disconnect_endpoint(argv[optind + 2]);
	else
		print_usage("Invalid disconnection type\n");

	return 0;
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  disconnect <pipeline|endpoint> <id>\n"
		"\n");
}

struct module module_disconnect =
{
	.cmd	= "disconnect",
	.do_it	= handle_disconnect,
	.usage	= usage,
};

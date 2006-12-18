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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <list.h>

#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "connect.h"

static int do_connect(const char *endpoint1_path, const char *endpoint2_path)
{
/*	int endpoint1_id, endpoint2_id;

	endpoint1_id = decode_endpoint_id(endpoint1_str);
	endpoint2_id = decode_endpoint_id(endpoint2_str);*/

	verbose("Connecting '%s' to '%s'...",
		endpoint1_path,
		endpoint2_path);

	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;

/* FIXME  FIXME  FIXME  FIXME  FIXME  FIXME  FIXME  FIXME  FIXME strncpy */

	/* Make sure both endpoints are disconnected */
	strcpy(connect.from_endpoint, endpoint1_path);
	strcpy(connect.to_endpoint, "");
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT_ENDPOINT, &connect) < 0) {

		if (errno != ENOTCONN) {
			fprintf(stderr,
				"ioctl(IOC_DISCONNECT_ENDPOINT) failed: %s\n",
				strerror(errno));

			return 1;
		}
	}

	strcpy(connect.from_endpoint, endpoint2_path);
	strcpy(connect.to_endpoint, "");
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT_ENDPOINT, &connect) < 0) {
		if (errno != ENOTCONN) {
			fprintf(stderr,
				"ioctl(IOC_DISCONNECT_ENDPOINT) failed: %s\n",
				strerror(errno));

			return 1;
		}
	}

	/* Now make the connection */
	strcpy(connect.from_endpoint, endpoint1_path);
	strcpy(connect.to_endpoint, endpoint2_path);
	connect.flags = VISDN_CONNECT_FLAG_PERMANENT;

	if (ioctl(fd, VISDN_IOC_CONNECT, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_CONNECT) failed: %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	verbose("Done!\n");

	return 0;
}

static int handle_connect(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing first endpoint\n");
	}

	if (argc <= optind + 2) {
		print_usage("Missing second endpoint\n");
	}

	return do_connect(argv[optind + 1], argv[optind + 2]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  connect <endpoint1> <endpoint2>\n"
		"\n"
		"    Connects two endpoint endpoints using the least-cost\n"
		"    path. The endpoint's legs are configured with a\n"
		"    compatible framing. The endpoints are not automatically\n"
		"    enabled, they should be explicitly enabled through\n"
		"    \"%s enable <endpoint1> <endpoint2>\".\n"
		"\n"
		"    <endpoint> is either numeric endpoint-id (e.g. 0003422)\n"
		"           or the fully qualified sysfs path (e.g.\n"
		"           /sys/bus/pci/devices/0000:00:01.0/st0/D)\n",
		argv[0]);
}

struct module module_connect =
{
	.cmd	= "connect",
	.do_it	= handle_connect,
	.usage	= usage,
};

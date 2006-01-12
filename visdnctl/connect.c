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

#include <linux/visdn/softcxc.h>
#include <linux/visdn/cxc.h>
#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "connect.h"

static int do_connect(const char *chan1_str, const char *chan2_str)
{
	int chan1_id, chan2_id;

	chan1_id = decode_chan_id(chan1_str);
	chan2_id = decode_chan_id(chan2_str);

	verbose("Connecting '%06d' to '%06d'...",
		chan1_id,
		chan2_id);

	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;

	/* Make sure both endpoints are disconnected */
	connect.src_chan_id = chan1_id;
	connect.dst_chan_id = 0;
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT_PATH, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_DISCONNECT_PATH) failed: %s\n",
			strerror(errno));

		return 1;
	}

	connect.src_chan_id = chan2_id;
	connect.dst_chan_id = 0;
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_DISCONNECT_PATH, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_DISCONNECT_PATH) failed: %s\n",
			strerror(errno));

		return 1;
	}

	/* Now make the connection */
	connect.src_chan_id = chan1_id;
	connect.dst_chan_id = chan2_id;
	connect.flags =
		VISDN_CONNECT_FLAG_PERMANENT |
		VISDN_CONNECT_FLAG_OVERRIDE;

	if (ioctl(fd, VISDN_IOC_CONNECT_PATH, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_CONNECT_PATH) failed: %s\n",
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
		print_usage("Missing first channel\n");
	}

	if (argc <= optind + 2) {
		print_usage("Missing second channel\n");
	}

	return do_connect(argv[optind + 1], argv[optind + 2]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  connect <chan1> <chan2>\n"
		"\n"
		"    Connects two endpoint channels using the least-cost\n"
		"    path. The channel's legs are configured with a\n"
		"    compatible framing. The channels are not automatically\n"
		"    enabled, they should be explicitly enabled through\n"
		"    \"%s enable <chan1> <chan2>\".\n"
		"\n"
		"    <chan> is either numeric channel-id (e.g. 0003422)\n"
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

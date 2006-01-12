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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <linux/visdn/softcxc.h>
#include <linux/visdn/cxc.h>
#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "disconnect.h"

int handle_disconnect(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1) {
		print_usage("Missing first channel ID\n");
	}

	int chan1_id = decode_chan_id(argv[optind + 1]);

	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;

	if (argc > optind + 2) {
		int chan2_id =  decode_chan_id(argv[optind + 2]);

		printf("Disconnecting '%06d' from '%06d'\n",
			chan1_id,
			chan2_id);

		connect.src_chan_id = chan1_id;
		connect.dst_chan_id = chan2_id;
		connect.flags = 0;

		if (ioctl(fd, VISDN_IOC_DISCONNECT, &connect) < 0) {
			fprintf(stderr, "ioctl(IOC_DISCONNECT) failed: %s\n",
				strerror(errno));

			return 1;
		}

	} else {
		printf("Disconnecting '%06d'\n",
			chan1_id);

		connect.src_chan_id = chan1_id;
		connect.dst_chan_id = 0;
		connect.flags = 0;

		if (ioctl(fd, VISDN_IOC_DISCONNECT_PATH, &connect) < 0) {
			fprintf(stderr,
				"ioctl(IOC_DISCONNECT_PATH) failed: %s\n",
				strerror(errno));

			return 1;
		}

	}

	close(fd);

	return 0;
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  disconnect <chan> [chan2]\n"
		"\n"
		"    Disconnects two endpoint channels staring from endpoint\n"
		"    <chan> and following the path. chan2 may be optionally\n"
		"    specified if chan is not an endpoint to indicate the\n"
		"    walk direction\n");
}

struct module module_disconnect =
{
	.cmd	= "disconnect",
	.do_it	= handle_disconnect,
	.usage	= usage,
};

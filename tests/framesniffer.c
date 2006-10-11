/*
 * vISDN
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include <linux/visdn/router.h>
#include <linux/visdn/userport.h>


int main(int argc, char *argv[])
{
	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	int router_control_fd = open("/dev/visdn/router-control", O_RDWR);
	if (router_control_fd < 0) {
		perror("Unable to open router-control");
		return 1;
	}

	int fd;
	fd = open("/dev/visdn/userport_hdlc", O_RDWR);
	if (fd < 0) {
		perror("cannot open /dev/visdn/userport_hdlc");
		return 1;
	}

	struct vup_ctl vup_ctl;
	if (ioctl(fd, VISDN_UP_GET_NODEID, (caddr_t)&vup_ctl) < 0) {
		perror("ioctl(VISDN_UP_GET_NODEID)");
		return 1;
	}

	char node_id[80];
	snprintf(node_id, sizeof(node_id), "/sys/%s", vup_ctl.node_id);

	struct visdn_connect vc;

	memset(&vc, 0, sizeof(vc));
	strncpy(vc.from_endpoint, argv[1],
				sizeof(vc.from_endpoint));
	strncpy(vc.to_endpoint, node_id,
				sizeof(vc.to_endpoint));

printf("Connect: %s => %s\n", vc.from_endpoint, vc.to_endpoint);

	if (ioctl(router_control_fd, VISDN_IOC_CONNECT, (caddr_t) &vc) < 0) {
		perror("ioctl(VISDN_CONNECT, br=>sp)");
		return 1;
	}

	int pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = pipeline_id;
	if (ioctl(router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						(caddr_t)&vc) < 0) {
		perror("ioctl(VISDN_PIPELINE_OPEN, br=>sp)");
		return 1;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = pipeline_id;
	if (ioctl(router_control_fd, VISDN_IOC_PIPELINE_START,
						(caddr_t)&vc) < 0) {
		perror("ioctl(VISDN_PIPELINE_START, br=>sp)");
		return 1;
	}

	char buf[4096];
	struct pollfd pollfd = { fd, POLLIN, 0 };

	while(1) {

		if (poll(&pollfd, 1, -1) < 0) {
			perror("poll");
			return 1;
		}

		int nread = read(fd, buf, sizeof(buf));
		printf(" %d: ", nread);

		int i;
		for (i=0; i<nread; i++)
			printf("%02x", *(__u8 *)(buf + i));

		printf("\n");
	}

	return 0;
}

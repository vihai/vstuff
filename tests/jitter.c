/*
 * vISDN
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <linux/visdn/router.h>
#include <linux/visdn/userport.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

double double_now()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void print_usage(const char *progname)
{
	fprintf(stderr,
		"%s [options]\n"
		"	Options may be:\n"
		"	--count=i	Samples count\n",
		progname);

	exit(1);
}


int main(int argc, char *argv[])
{
	int count = 1000;
	double class_step = 0.000001;
	double class_start = 0.0199;
	double class_end = 0.0201;
	int nclasses = (class_end - class_start) / class_step;

	struct option options[] = {
		{ "count", required_argument, 0, 0 },
		{ }
	};

	int c;
	int optidx;

	for(;;) {
		c = getopt_long(argc, argv, "", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 0 &&
		    !strcmp(options[optidx].name, "count")) {
				count = atoi(optarg);
		} else {
			fprintf(stderr,"Unknow option %s\n",
				options[optidx].name);

			print_usage(argv[0]);

			return 1;
		}
	}

	int fd1;
	fd1 = open("/dev/visdn/userport_stream", O_RDWR);
	if (fd1 < 0) {
		perror("cannot open /dev/visdn/userport_stream");
		return 1;
	}

	int fd2;
	fd2 = open("/dev/visdn/userport_stream", O_RDWR);
	if (fd2 < 0) {
		perror("cannot open /dev/visdn/userport_stream");
		return 1;
	}

	int router_control_fd = open("/dev/visdn/router-control", O_RDWR);
	if (router_control_fd < 0) {
		perror("Unable to open router-control");
		return 1;
	}

	struct vup_ctl vup_ctl;
	if (ioctl(fd1, VISDN_UP_GET_NODEID, (caddr_t)&vup_ctl) < 0) {
		perror("ioctl(VISDN_UP_GET_NODEID)");
		return 1;
	}

	char node1_id[80];
	snprintf(node1_id, sizeof(node1_id), "/sys/%s", vup_ctl.node_id);

	printf("Created userport: %s\n", node1_id);

	if (ioctl(fd2, VISDN_UP_GET_NODEID, (caddr_t)&vup_ctl) < 0) {
		perror("ioctl(VISDN_UP_GET_NODEID)");
		return 1;
	}

	char node2_id[80];
	snprintf(node2_id, sizeof(node2_id), "/sys/%s", vup_ctl.node_id);

	printf("Created userport: %s\n", node2_id);

	struct visdn_connect vc;
	memset(&vc, 0, sizeof(vc));
	strncpy(vc.from_endpoint, node2_id,
				sizeof(vc.from_endpoint));
	strncpy(vc.to_endpoint, node1_id,
				sizeof(vc.to_endpoint));

	if (ioctl(router_control_fd, VISDN_IOC_CONNECT, (caddr_t) &vc) < 0) {
		perror("ioctl(VISDN_CONNECT, sp=>br)");
		return 1;
	}

	int pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = pipeline_id;
	if (ioctl(router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						(caddr_t)&vc) < 0) {
		perror("ioctl(VISDN_PIPELINE_OPEN, sp=>br)");
		return 1;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = pipeline_id;
	if (ioctl(router_control_fd, VISDN_IOC_PIPELINE_START,
						(caddr_t)&vc) < 0) {
		perror("ioctl(VISDN_PIPELINE_START, sp=>br)");
		return 1;
	}

	printf("This program calculates a statistic on userland jitter\n"
		"polling the timer device. Calculating...\n");

	struct pollfd pollfd = { fd1, POLLIN, 0 };
	char junk;

	// Synchronize with the timer
	if (poll(&pollfd, 1, -1) < 0) {
		perror("poll");
		return 1;
	}

	read(fd1, &junk, 1);

	int *frequencies = (int *)malloc(nclasses * sizeof(*frequencies));
	int i;
	for(i=0; i<nclasses; i++)
		frequencies[i] = 0;

	double mean = 0;
	double varsum = 0;
	double max_delay = 0;
	double min_delay = 1000;
	double prepoll = double_now();
	for (i=0; i < count; i++) {
		if (poll(&pollfd, 1, -1) < 0) {
			perror("poll");
			return 1;
		}

		read(fd1, &junk, 1);

		double delay = double_now() - prepoll;
		prepoll = double_now();

		double delta = delay - mean;
		mean += delta / (i + 1);
		varsum += delta * (delay - mean);

		if (delay > max_delay)
			max_delay = delay;

		if (delay < min_delay)
			min_delay = delay;

		frequencies[(min(max((int)((delay - class_start) / class_step),
							0), nclasses - 1))]++;
	}

	double variance = varsum / (count - 1);

	printf("average: %.3fms\n", mean * 1000.0);
	printf("stddev: %.3fms\n",  sqrt(variance) * 1000.0);
	printf("max: %.3fms\n", max_delay * 1000.0);
	printf("min: %.3fms\n", min_delay * 1000.0);

	for (i=0; i<nclasses; i++)
		printf("%6.3f %6d\n",
			(i * class_step + class_start) * 1000,
			frequencies[i]);
	printf("\n");

	return 0;
}

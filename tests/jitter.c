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
#include <sys/poll.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

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
	double class_start = 0.0099;
	double class_end = 0.0101;
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

	int fd;
	fd = open("/dev/visdn/timer", O_RDONLY);
	if (fd < 0) {
		printf("cannot open /dev/visdn/timer: %s\n", strerror(errno));

		return 1;
	}

	printf("This program calculates a statistic on userland jitter polling\n"
		"the timer device. Calculating...\n");

	struct pollfd pollfd = { fd, POLLIN, 0 };

	// Synchronize with the timer
	if (poll(&pollfd, 1, -1) < 0) {
		printf("poll; %s\n", strerror(errno));
		return 1;
	}

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
			printf("poll: %s\n", strerror(errno));
			return 1;
		}

		double delay = double_now() - prepoll;
		prepoll = double_now();

		double delta = delay - mean;
		mean += delta / (i + 1);
		varsum += delta * (delay - mean);

		if (delay > max_delay)
			max_delay = delay;

		if (delay < min_delay)
			min_delay = delay;

		frequencies[(int)(min(max((delay - class_start) / class_step, 0),
				nclasses - 1))]++;
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
}

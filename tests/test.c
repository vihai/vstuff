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
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <linux/types.h>
#include <assert.h>
#include <poll.h>

#include <netinet/in.h>

#include <getopt.h>

#include <lapd.h>

struct opts
{
	const char *intf_name;

	int frame_size;
	int interval;

	int socket_debug;
};

void print_usage(const char *progname)
{
	fprintf(stderr,
		"%s: <interface>\n"
		"	[-d|--debug]\n", progname);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct opts opts;

	memset(&opts, 0x00, sizeof(opts));

	struct option options[] = {
		{ "debug", no_argument, 0, 0 },
		{ }
	};

	int c;
	int optidx;

	for(;;) {
		c = getopt_long(argc, argv, "d", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 'd' || (c == 0 &&
		    !strcmp(options[optidx].name, "debug"))) {
			opts.socket_debug = 1;
		} else {
			fprintf(stderr,
				"Unknow option %s\n", options[optidx].name);
			print_usage(argv[0]);
			return 1;
		}
	}

	if (optind < argc) {
		opts.intf_name = argv[optind];
	} else {
		fprintf(stderr,"Missing required interface name\n");
		print_usage(argv[0]);
	}

	printf("Opening socket... ");
	int s = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (s < 0) {
		printf("socket: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	if (opts.socket_debug) {
		int on=1;

		printf("Putting socket in debug mode... ");
		if (setsockopt(s, SOL_SOCKET, SO_DEBUG,
			             &on, sizeof(on)) < 0) {
			printf("setsockopt: %s\n", strerror(errno));
			exit(1);
		}
		printf("OK\n");
	}

	printf("Binding to %s... ", opts.intf_name);
	if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
		             opts.intf_name, strlen(opts.intf_name)+1) < 0) {
		printf("setsockopt: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	int role;
	int optlen=sizeof(role);
	if (getsockopt(s, SOL_LAPD, LAPD_ROLE,
	    &role, &optlen)<0) {
		printf("getsockopt: %s\n", strerror(errno));
		exit(1);
	}

	printf("Role... ");
	if (role == LAPD_ROLE_TE) {
		printf("TE\n");
	} else if (role == LAPD_ROLE_NT) {
		printf("TE role only\n");
		return 1;
	} else
		printf("Unknown role %d\n", role);

	printf("Binding TEI...");
	struct sockaddr_lapd sal;
	sal.sal_family = AF_LAPD;
	sal.sal_tei = LAPD_DYNAMIC_TEI;

	if (bind(s, (struct sockaddr *)&sal, sizeof(sal)) < 0) {
		printf("bind(): %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	printf("Connecting...");
	if (connect(s, NULL, 0) < 0) {
		printf("connect: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	struct pollfd polls;

	polls.fd = s;
	polls.events = POLLIN|POLLERR;

	for (;;) {
		if (poll(&polls, 1, 0) < 0) {
			printf("poll: %s\n", strerror(errno));
			exit(1);
		}

		if (polls.revents & POLLERR) {
			printf("POLLERR\n");
		}

		if (polls.revents & POLLIN) {
			printf("POLLIN\n");
		}
	}
}

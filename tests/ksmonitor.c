/*
 *
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <getopt.h>

#include <list.h>

#include <libkstreamer.h>

#define min(a,b) ((a) > (b) ? (b) : (a))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

void print_usage(const char *progname)
{
	fprintf(stderr,
"%s: \n"
"	(--)\n"
"\n",
		progname);

	exit(1);
}

BOOL have_to_exit = FALSE;

void sig_handler(int sig)
{
	have_to_exit = TRUE;
}

int main(int argc, char *argv[])
{
	int err;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);
	setvbuf(stderr, (char *)NULL, _IONBF, 0);

	struct ks_conn *conn;
	conn = ks_conn_create();
	if (!conn) {
		fprintf(stderr, "Cannot initialize kstreamer library\n");
		return 1;
	}

	err = ks_conn_establish(conn);
	if (err < 0) {
		fprintf(stderr, "Cannot connect kstreamer library\n");
		return 1;
	}

	ks_update_topology(conn);

	while(!have_to_exit)
		sleep(3600);

	ks_conn_destroy(conn);

	return 0;
}

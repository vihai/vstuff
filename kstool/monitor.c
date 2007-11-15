/*
 * kstreamer's controlling program
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
#include <signal.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <list.h>

#include <libkstreamer.h>

#include "kstool.h"
#include "monitor.h"
#include "xml.h"

BOOL have_to_exit = FALSE;

static void sig_handler(int sig)
{
	have_to_exit = TRUE;
}

static int handle_monitor(int optind)
{
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGQUIT, sig_handler);

	glob.conn->topology_event_callback = topology_event_handler_xml;

	ks_update_topology(glob.conn);

	while(!have_to_exit)
		sleep(3600);

	return 0;
}

static void usage()
{
	fprintf(stderr,
		"  monitor\n"
		"\n"
		"    Monitors kstreamer events and outputs them in XML-like\n"
		"    format. An initial resync is also performed.\n");
}

struct module module_monitor =
{
	.cmd	= "monitor",
	.do_it	= handle_monitor,
	.usage	= usage,
};

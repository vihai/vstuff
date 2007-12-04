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

#include <libkstreamer/libkstreamer.h>

#include "kstool.h"
#include "dump.h"
#include "xml.h"

static int handle_dump(int optind)
{
	glob.conn->topology_event_callback = topology_event_handler_xml;

	ks_update_topology(glob.conn);

	return 0;
}

static void usage()
{
	fprintf(stderr,
		"  dump\n"
		"\n"
		"    Dumps current topology in XML-like format\n");
}

struct module module_dump =
{
	.cmd	= "dump",
	.do_it	= handle_dump,
	.usage	= usage,
};

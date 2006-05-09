/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 * This file is inspired and slightly copied from other pppd plugins
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "pppd.h"
#include "fsm.h"
#include "lcp.h"

#include <linux/visdn/cxc.h>
#include <linux/visdn/router.h>
#include <linux/visdn/ppp.h>

static int device_got_set = 0;

char pppd_version[] = VERSION;

extern int new_style_driver;	/* From sys-linux.c */

static int visdn_setdevname(char *cmd, char **argv, int doit);

static option_t visdn_options[] = {
	{ "device name", o_wild, (void *) &visdn_setdevname, "vISDN device name",
		OPT_DEVNAM | OPT_PRIVFIX | OPT_NOARG | OPT_A2STRVAL | OPT_STATIC, devnam},
	{ NULL }
};


struct channel visdn_channel;

/* returns:
 *  -1 if there's a problem with setting the device
 *   0 if we can't parse "cp" as a valid name of a device
 *   1 if "cp" is a reasonable thing to name a device
 * Note that we don't actually open the device at this point
 * We do need to fill in:
 *   devnam: a string representation of the device
 *   devstat: a stat structure of the device.
 */
static int visdn_setdevname(char *cmd, char **argv, int doit)
{
	if (device_got_set)
		return 1;

	dbglog("PPPovISDN visdn_setdevname %s", cmd);

	strlcpy(devnam, cmd, sizeof(devnam));

	device_got_set = 1;

	if (the_channel != &visdn_channel) {
		the_channel = &visdn_channel;
		modem = 0;
		lcp_wantoptions[0].neg_accompression = 0;
		lcp_allowoptions[0].neg_accompression = 0;
		lcp_wantoptions[0].neg_asyncmap = 0;
		lcp_allowoptions[0].neg_asyncmap = 0;
		lcp_wantoptions[0].neg_pcompression = 0;
	}

	return 1;
}

static void visdn_process_extra_options(void)
{
	dbglog("visdn_process_extra_options");
}

#include <unistd.h>

int control_fd;
int ppp_channel_id;
int ppp_conn_id;

static int visdn_connect(void)
{
	int fd;
	dbglog("PPPovISDN - visdn_connect('%s')", devnam);

	if (!device_got_set)
		fatal("No device specified");

	control_fd = open("/dev/visdn/router-control", O_RDWR);
	if (control_fd < 0)
		fatal("failed to open router-control: %m");

	fd = open("/dev/visdn/ppp", O_RDWR);
	if (fd < 0)
		fatal("failed to open ppp-control device: %m");

	if (ioctl(fd, VISDN_PPP_GET_CHANID,
			(caddr_t)&ppp_channel_id) < 0)
		fatal("ioctl(VISDN_IOC_GET_CHANID): %m");

	dbglog("PPPovISDN - control device opened successfully");

	struct visdn_connect vc;
	memset(&vc, 0, sizeof(vc));
	vc.src_chan_id = ppp_channel_id;
	vc.dst_chan_id = atoi(devnam);

	if (ioctl(fd, VISDN_IOC_CONNECT, (caddr_t)&vc) < 0)
		fatal("ioctl(VISDN_CONNECT): %m\n");

	ppp_conn_id = vc.pipeline_id;

	if (ioctl(fd, VISDN_IOC_PIPELINE_OPEN, NULL) < 0)
		fatal("ioctl(VISDN_PIPELINE_OPEN): %m\n");

	if (ioctl(fd, VISDN_IOC_PIPELINE_START, NULL) < 0)
		fatal("ioctl(VISDN_PIPELINE_START): %m\n");

	dbglog("PPPovISDN - channel connected to ppp device");

	strlcpy(ppp_devnam, devnam, sizeof(ppp_devnam));

	return fd;
}

static void visdn_disconnect(void)
{
	dbglog("PPPovISDN - visdn_disconnect");
}

static void visdn_send_config(int mtu, u_int32_t asyncmap, int pcomp, int accomp)
{
	dbglog("PPPovISDN - visdn_send_config");
}

static void visdn_recv_config(int mru, u_int32_t asyncmap, int pcomp, int accomp)
{
	dbglog("PPPovISDN - visdn_recv_config");
}

void plugin_init(void)
{
	if (!ppp_available() && !new_style_driver)
		fatal("Kernel doesn't support ppp_generic - needed for PPP over vISDN");

	add_options(visdn_options);

	dbglog("vISDN plugin_init");
}

struct channel visdn_channel = {
	options:		visdn_options,
	process_extra_options:	visdn_process_extra_options,
	check_options:		NULL,
	connect:		visdn_connect,
	disconnect:		visdn_disconnect,
	establish_ppp:		generic_establish_ppp,
	disestablish_ppp:	generic_disestablish_ppp,
	send_config:		visdn_send_config,
	recv_config:		visdn_recv_config,
	close:			NULL,
	cleanup:		NULL
};


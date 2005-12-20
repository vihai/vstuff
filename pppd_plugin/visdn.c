/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
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
//	extern struct stat devstat;

//	if (device_got_set)
//		return 1;

	dbglog("PPPovISDN visdn_setdevname %s", cmd);

	strlcpy(devnam, cmd, sizeof(devnam));

//	devstat.st_mode = S_IFSOCK;

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

static int visdn_connect(void)
{
	int fd;
	dbglog("PPPovISDN - visdn_connect('%s')", devnam);

	if (!device_got_set)
		fatal("No device specified");

	fd = open("/dev/visdn/ppp", O_RDWR);
	if (fd < 0)
		fatal("failed to open ppp-control device: %m");

	dbglog("PPPovISDN - control device opened successfully");

	struct visdn_connect vc;
	vc.src_chan_id = 0;
	vc.dst_chan_id = atoi(devnam);
	vc.flags = 0;

	if (ioctl(fd, VISDN_IOC_CONNECT_PATH, (caddr_t)&vc) < 0) {
		fatal("ioctl(VISDN_CONNECT_PATH): %m\n");
	}

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

//	int sock;
//	struct ifreq ifr;
/*
	if (mtu > visdn_max_mtu)
		error("Couldn't increase MTU to %d", mtu);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		fatal("Couldn't create IP socket: %m");
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_mtu = mtu;
	if (ioctl(sock, SIOCSIFMTU, (caddr_t) &ifr) < 0)
		fatal("ioctl(SIOCSIFMTU): %m");
	(void) close (sock);*/
}

static void visdn_recv_config(int mru, u_int32_t asyncmap, int pcomp, int accomp)
{
	dbglog("PPPovISDN - visdn_recv_config");

//	if (mru > visdn_max_mru)
//		error("Couldn't increase MRU to %d", mru);
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


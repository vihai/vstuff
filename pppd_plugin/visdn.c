/*
 * vISDN PPP plugin
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
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

#include <linux/kstreamer/ppp.h>

#include <linux/kstreamer/hdlc_framer.h>

#include <libskb.h>
#include <libkstreamer.h>

#include "pppd.h"
#include "fsm.h"
#include "lcp.h"

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

static int visdn_connect(void)
{
	int err;

	int fd;

	dbglog("PPPovISDN - visdn_connect('%s')", devnam);

	if (!device_got_set)
		fatal("No device specified");

	struct ks_conn *conn;
	conn = ks_conn_create();
	if (!conn)
		fatal("Cannot initialize kstreamer library");

	dbglog("PPPovISDN - 1");

	err = ks_conn_establish(conn);
	if (err < 0)
		fatal("Cannot connect kstreamer library");

	ks_update_topology(conn);
	dbglog("PPPovISDN - 2");

	struct ks_feature *hdlc_framer;
	hdlc_framer = ks_feature_get_by_name(conn, "hdlc_framer");
	if (!hdlc_framer)
		fatal("No HDLC framer attribute found");

	struct ks_feature *hdlc_deframer;
	hdlc_deframer = ks_feature_get_by_name(conn, "hdlc_deframer");
	if (!hdlc_deframer)
		fatal("No HDLC deframer attribute found");

	fd = open("/dev/ks/ppp", O_RDWR);
	if (fd < 0)
		fatal("failed to open ppp-control device: %m");

	ks_conn_sync(conn);

	int ppp_node_id;
	if (ioctl(fd, VISDN_PPP_GET_CHANID,
			(caddr_t)&ppp_node_id) < 0)
		fatal("ioctl(VISDN_IOC_GET_CHANID): %m");

	struct ks_node *node_bearer;
	node_bearer = ks_node_get_by_id(conn, atoi(devnam));
	if (!node_bearer)
		fatal("Cannot find bearer node");

	struct ks_node *node_ppp;
	node_ppp = ks_node_get_by_id(conn, ppp_node_id);
	if (!node_ppp)
		fatal("Cannot find PPP node");


	dbglog("PPPovISDN - control device opened successfully");

	/***************** IN PIPELINE ********************/
	struct ks_pipeline *in_pipeline;
	in_pipeline = ks_pipeline_alloc();
	if (!in_pipeline)
		fatal("Cannot alloc in pipeline");

	err = ks_pipeline_autoroute(in_pipeline, conn, node_bearer, node_ppp);
	if (err < 0)
		fatal("Cannot connect nodes: %s", strerror(-err));

	in_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;

	err = ks_pipeline_create(in_pipeline, conn);
	if (err < 0)
		fatal("Cannot create pipeline: %s", strerror(-err));

	int i;
	struct ks_hdlc_deframer_descr *in_hdlc_deframer = NULL;
	for(i=0; i<in_pipeline->chans_cnt; i++) {
		struct ks_chan *chan = in_pipeline->chans[i];

		struct ks_feature_value *featval;
		list_for_each_entry(featval, &chan->features, node) {

			if (featval->feature == hdlc_deframer) {

				struct ks_hdlc_deframer_descr *descr =
					(struct ks_hdlc_deframer_descr *)
					featval->payload;

				if (!in_hdlc_deframer ||
				    descr->hardware)
					in_hdlc_deframer = descr;
			}
		}
	}

	if (!in_hdlc_deframer)
		fatal("No HDLC framer along the pipeline");

	in_hdlc_deframer->enabled = TRUE;

	ks_pipeline_update_chans(in_pipeline, conn);

	in_pipeline->status = KS_PIPELINE_STATUS_FLOWING;
	err = ks_pipeline_update(in_pipeline, conn);
	if (err < 0)
		fatal("Cannot start the in pipeline");










	/***************** OUT PIPELINE ********************/

	struct ks_pipeline *out_pipeline;
	out_pipeline = ks_pipeline_alloc();
	if (!out_pipeline)
		fatal("Cannot alloc in pipeline");

	err = ks_pipeline_autoroute(out_pipeline, conn, node_ppp, node_bearer);
	if (err < 0)
		fatal("Cannot connect nodes: %s", strerror(-err));

	out_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;
	err = ks_pipeline_create(out_pipeline, conn);
	if (err < 0)
		fatal("Cannot create pipeline: %s", strerror(-err));

	struct ks_hdlc_framer_descr *out_hdlc_framer = NULL;
	for(i=0; i<out_pipeline->chans_cnt; i++) {
		struct ks_chan *chan = out_pipeline->chans[i];

		struct ks_feature_value *featval;
		list_for_each_entry(featval, &chan->features, node) {

			if (featval->feature == hdlc_framer) {

				struct ks_hdlc_framer_descr *descr =
					(struct ks_hdlc_framer_descr *)
					featval->payload;

				if (!out_hdlc_framer ||
				    descr->hardware)
					out_hdlc_framer = descr;
			}
		}
	}

	if (!out_hdlc_framer)
		fatal("No HDLC deframer along the pipeline");

	out_hdlc_framer->enabled = TRUE;

	ks_pipeline_update_chans(out_pipeline, conn);

	out_pipeline->status = KS_PIPELINE_STATUS_FLOWING;
	err = ks_pipeline_update(out_pipeline, conn);
	if (err < 0)
		fatal("Cannot start the pipeline");





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


/*
 * kstreamer's controlling program
 *
 * Copyright (C) 2005-2007 Daniele Orlandi
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
#include <limits.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <libkstreamer.h>

#include <linux/kstreamer/hdlc_framer.h>
#include <linux/kstreamer/octet_reverser.h>

#include "kstool.h"
#include "connect.h"

static int apply_parameters_to_autorouted(
	struct ks_pipeline *pipeline,
	int start_nchans,
	int end_nchans,
	struct ks_pd_parameters *pars)
{
	/* Find the channels providing the "best" (hardware if possibile)
	 * implementation of required attributes
	 */
	struct ks_chan *hdlc_framer_chan = NULL;
	struct ks_hdlc_framer_descr *hdlc_framer_descr = NULL;

	struct ks_chan *hdlc_deframer_chan = NULL;
	struct ks_hdlc_deframer_descr *hdlc_deframer_descr = NULL;

	struct ks_chan *octet_reverser_chan = NULL;
	struct ks_octet_reverser_descr *octet_reverser_descr = NULL;

	int i;

	for(i=start_nchans; i<=end_nchans; i++) {
		struct ks_chan *chan = pipeline->chans[i];

		struct ks_dynattr_instance *dynattr;
		list_for_each_entry(dynattr, &chan->dynattrs, node) {

			if (dynattr->dynattr == glob.octet_reverser) {

				struct ks_octet_reverser_descr *descr =
					(struct ks_octet_reverser_descr *)
					dynattr->payload;

				if (!octet_reverser_chan || descr->hardware) {
					octet_reverser_chan = chan;
					octet_reverser_descr = descr;
				}

			} else if (dynattr->dynattr == glob.hdlc_framer) {

				struct ks_hdlc_framer_descr *descr =
					(struct ks_hdlc_framer_descr *)
					dynattr->payload;

				if (!hdlc_framer_chan || descr->hardware) {
					hdlc_framer_chan = chan;
					hdlc_framer_descr = descr;
				}

			} else if (dynattr->dynattr == glob.hdlc_deframer) {

				struct ks_hdlc_deframer_descr *descr =
					(struct ks_hdlc_deframer_descr *)
					dynattr->payload;

				if (!hdlc_deframer_chan || descr->hardware) {
					hdlc_deframer_chan = chan;
					hdlc_deframer_descr = descr;
				}
			}
		}
	}

	/* Apply attributes to channels */
        struct ks_pd_parameter *par;
	list_for_each_entry(par, &pars->list, node) {
		if (!strcmp(par->name->text, "hdlc_framer")) {

			if (!glob.hdlc_framer) {
				fprintf(stderr,
					"No HDLC framer attribute found\n");
				return -ENODEV;
			}

			if (!hdlc_framer_chan) {
				fprintf(stderr,
					"No HDLC framer along the"
					" pipeline\n");
				return -ENODEV;
			}

			hdlc_framer_descr->enabled = !!atoi(par->value->text);

		} else if (!strcmp(par->name->text, "hdlc_deframer")) {
			if (!glob.hdlc_deframer) {
				fprintf(stderr,
					"No HDLC deframer attribute found\n");
				return -ENODEV;
			}

			if (!hdlc_deframer_chan) {
				fprintf(stderr,
					"No HDLC deframer along the"
					" pipeline\n");
				return -ENODEV;
			}

			hdlc_deframer_descr->enabled = !!atoi(par->value->text);

		} else if (!strcmp(par->name->text, "octet_reverser")) {

			if (!glob.octet_reverser) {
				fprintf(stderr,
					"No octet reverser attribute found\n");
				return -ENODEV;
			}

			if (!octet_reverser_chan) {
				fprintf(stderr,
					"No octet reverser along the"
					" pipeline\n");
				return -ENODEV;
			}

			hdlc_framer_descr->enabled = !!atoi(par->value->text);
		}
        }

	return 0;
}

static int do_connect(const char *pipeline_descr)
{
	int err;

	struct ks_pd *pd;
	pd = ks_pd_parse(pipeline_descr);
	if (!pd || pd->failed) {
		fprintf(stderr, "Cannot parse pipeline description\n");
		return 1;
	}

	ks_pd_dump(pd, glob.conn, KS_LOG_DEBUG);

	struct ks_pipeline *pipeline;
	pipeline = ks_pipeline_alloc();
	if (!pipeline) {
		fprintf(stderr, "Cannot allocate pipeline\n");
		return 1;
	}

	struct ks_node *ep1;
	ep1 = ks_node_get_by_token(glob.conn, pd->pipeline->ep1);
	if (!ep1) {
		fprintf(stderr,
			"Cannot find endpoint 1 '%s'\n",
			pd->pipeline->ep1->text);
		return 1; // Cleanup - FIXME
	}

	struct ks_node *ep2;
	ep2 = ks_node_get_by_token(glob.conn, pd->pipeline->ep2);
	if (!ep2) {
		fprintf(stderr,
			"Cannot find endpoint 2 '%s'\n",
			pd->pipeline->ep2->text);
		return 1; // Cleanup - FIXME
	}

	struct ks_pd_channel *pd_chan;
	list_for_each_entry(pd_chan, &pd->pipeline->channels->list, node) {

		if (pd_chan->name->id == TK_IDENTIFIER &&
		    !strcmp(pd_chan->name->text, KS_AUTOROUTE_TOKEN)) {

			int start_nchans = pipeline->chans_cnt;
			struct ks_node *src_node;
			struct ks_node *dst_node;

			if (pipeline->chans_cnt)
				src_node = pipeline->chans[
						pipeline->chans_cnt - 1]->to;
			else
				src_node = ep1;

			/* Give a peek at the next channel */
			if (pd_chan->node.next !=
					&pd->pipeline->channels->list) {

				struct ks_pd_channel *next_pd_chan;
				next_pd_chan = list_entry(pd_chan->node.next,
						struct ks_pd_channel, node);

				struct ks_chan *dst_chan;
				dst_chan = ks_chan_get_by_token(glob.conn,
							next_pd_chan->name);
				if (!dst_chan) {
					fprintf(stderr,
						"Cannot find channel '%s'\n",
						pd_chan->name->text);
					return 1; // Cleanup - FIXME
				}

				dst_node = dst_chan->from;

				ks_chan_put(dst_chan);
			} else
				dst_node = ep2;

			err = ks_pipeline_autoroute(pipeline, glob.conn,
				src_node, dst_node);
			if (err < 0) {
				fprintf(stderr,
					"Cannot autoroute channels: %s\n",
					strerror(-err));
				return 1;
			}

			if (pd_chan->parameters) {
				err = apply_parameters_to_autorouted(
						pipeline, start_nchans,
						pipeline->chans_cnt - 1,
						pd_chan->parameters);
				if (err < 0)
					return 1;
			}
		} else {
			struct ks_chan *chan;
			chan = ks_chan_get_by_token(glob.conn, pd_chan->name);
			if (!chan) {
				fprintf(stderr,
					"Cannot find channel '%s'\n",
					pd_chan->name->text);
				return 1; // Cleanup - FIXME
			}

			pipeline->chans[pipeline->chans_cnt] = chan;
			pipeline->chans_cnt++;
		}
	}

	ks_pipeline_dump(pipeline, glob.conn, KS_LOG_INFO);

	pipeline->status = KS_PIPELINE_STATUS_CONNECTED;

	err = ks_pipeline_create(pipeline, glob.conn);
	if (err < 0) {
		fprintf(stderr,
			"Cannot create pipeline: %s\n",
				strerror(-err));
		return 1;
	}

	ks_pipeline_update_chans(pipeline, glob.conn);

	printf("pipeline: %06x\n", pipeline->id);

	ks_pipeline_put(pipeline);

	verbose("Done!\n");

	return 0;
}

static int handle_connect(int optind)
{
	if (glob.argc <= optind + 1) {
		print_usage("Missing pipeline description\n");
	}

	return do_connect(glob.argv[optind + 1]);
}

static void usage()
{
	fprintf(stderr,
		"  pipeline_new <pipeline-descr>\n"
		"\n"
		"\n"
		"    <endpoint> is either numeric endpoint-id (e.g. 0003422)\n"
		"           or the fully qualified sysfs path (e.g.\n"
		"           /sys/bus/pci/devices/0000:00:01.0/st0/D)\n");
}

struct module module_connect =
{
	.cmd	= "connect",
	.do_it	= handle_connect,
	.usage	= usage,
};

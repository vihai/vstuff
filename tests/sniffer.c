/*
 * kstreamer sniffer
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <getopt.h>

#include <linux/kstreamer/dynattr.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/netlink.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/userport.h>

#include <linux/kstreamer/hdlc_framer.h>
#include <linux/kstreamer/octet_reverser.h>
#include <linux/kstreamer/amu_compander.h>

#include <list.h>

#include <libskb.h>
#include <libkstreamer.h>

#define DEBUG_CODE 1

#define min(a,b) ((a) > (b) ? (b) : (a))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

enum write_from
{
	WRITE_FROM_NONE,
	WRITE_FROM_STDIN,
	WRITE_FROM_FILE,
	WRITE_FROM_LOOPBACK,
};

struct opts
{
	BOOL debug;
	BOOL debug_state;
	BOOL debug_netlink;
	BOOL debug_router;

	const char *node_name;
	BOOL framed;

	BOOL read_to_stdout;
	BOOL read_to_stdout_binary;
	const char *read_to_file;

	enum write_from write_from;
	const char *write_from_file;

	struct ks_dynattr *hdlc_framer;
	struct ks_dynattr *hdlc_deframer;
	struct ks_dynattr *octet_reverser;
	struct ks_dynattr *amu_compander;

	BOOL enable_rx_octet_reverser;
	struct ks_octet_reverser_descr *rx_octet_reverser;

	BOOL enable_rx_hdlc_deframer;
	struct ks_hdlc_deframer_descr *rx_hdlc_deframer;

	BOOL enable_rx_amu_compander;
	BOOL rx_amu_compander_mu_mode;
	struct ks_amu_decompander_descr *rx_amu_compander;

	BOOL enable_tx_octet_reverser;
	struct ks_octet_reverser_descr *tx_octet_reverser;

	BOOL enable_tx_hdlc_framer;
	struct ks_hdlc_framer_descr *tx_hdlc_framer;

	BOOL enable_tx_amu_decompander;
	BOOL tx_amu_decompander_mu_mode;
	struct ks_amu_decompander_descr *tx_amu_decompander;

} opts =
{
	.write_from = WRITE_FROM_NONE,
};

void print_usage(const char *progname)
{
	fprintf(stderr,
"%s: [options] <channel>\n"
"\n"
"	--debug			Enable generic debugging\n"
"	--debug-state		Enable kstreamer's state debugging\n"
"	--debug-netlink		Enable kstreamer's netlink messages debugging\n"
"	--debug-router		Enable kstreamer's router debugging\n"
"	--read-to-stdout	Output stream to stdout\n"
"	--read-to-file <file>	Output stream to file\n"
"	--write-from <file>	Write from file\n"
"	--framed		Enable framed mode\n"
"	--rx-hdlc-deframer	Enable RX HDLC deframer\n"
"	--rx-octet-reverser	Enable RX octet reverser\n"
"	--tx-hdlc-framer	Enable TX HDLC framer\n"
"	--tx-octet-reverser	Enable TX octet reverser\n"
"	--rx-compander <alaw|mulaw>	Enable RX compander\n"
"	--tx-compander <alaw|mulaw>	Enable TX compander\n",
		progname);
}

int configure_rx_pipeline(struct ks_pipeline *pipeline)
{

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];

		struct ks_dynattr_instance *dynattr;
		list_for_each_entry(dynattr, &chan->dynattrs, node) {

			if (dynattr->dynattr == opts.octet_reverser) {

				struct ks_octet_reverser_descr *descr =
					(struct ks_octet_reverser_descr *)
					dynattr->payload;

				if (!opts.rx_octet_reverser ||
				    descr->hardware)
					opts.rx_octet_reverser = descr;

			} else if (dynattr->dynattr == opts.hdlc_deframer) {

				struct ks_hdlc_deframer_descr *descr =
					(struct ks_hdlc_deframer_descr *)
					dynattr->payload;

				if (!opts.rx_hdlc_deframer ||
				    descr->hardware)
					opts.rx_hdlc_deframer = descr;

			} else if (dynattr->dynattr == opts.amu_compander) {

				struct ks_amu_decompander_descr *descr =
					(struct ks_amu_decompander_descr *)
					dynattr->payload;

				if (!opts.rx_amu_compander)
					opts.rx_amu_compander = descr;
			}
		}
	}

	if (opts.enable_rx_hdlc_deframer) {
		if (!opts.hdlc_deframer) {
			fprintf(stderr,
				"No HDLC framer attribute found\n");
			return -ENODEV;
		}

		if (!opts.rx_hdlc_deframer) {
			fprintf(stderr,
				"No HDLC framer along the pipeline\n");
			return -ENODEV;
		}

		opts.rx_hdlc_deframer->enabled = TRUE;
	}

	if (opts.enable_rx_octet_reverser) {
		if (!opts.octet_reverser) {
			fprintf(stderr, "No octet reverser attribute found\n");
			return -ENODEV;
		}

		if (!opts.rx_octet_reverser) {
			fprintf(stderr,
				"No octet reverser along the pipeline\n");
			return -ENODEV;
		}

		opts.rx_octet_reverser->enabled = TRUE;
	}

	if (opts.enable_rx_amu_compander) {
		if (!opts.amu_compander) {
			fprintf(stderr, "No amu_decompander attribute found\n");
			return -ENODEV;
		}

		if (!opts.rx_amu_compander) {
			fprintf(stderr,
				"No amu_decompander along the pipeline\n");
			return -ENODEV;
		}

		opts.rx_amu_compander->enabled = TRUE;
		opts.rx_amu_compander->mu_mode =
					opts.rx_amu_compander_mu_mode;
	}

	return 0;
}

int configure_tx_pipeline(struct ks_pipeline *pipeline)
{

	int i;

	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];

		struct ks_dynattr_instance *dynattr;
		list_for_each_entry(dynattr, &chan->dynattrs, node) {

			if (dynattr->dynattr == opts.octet_reverser) {

				struct ks_octet_reverser_descr *descr =
					(struct ks_octet_reverser_descr *)
					dynattr->payload;

				if (!opts.tx_octet_reverser ||
				    descr->hardware)
					opts.tx_octet_reverser = descr;

			} else if (dynattr->dynattr == opts.hdlc_framer) {

				struct ks_hdlc_framer_descr *descr =
					(struct ks_hdlc_framer_descr *)
					dynattr->payload;

				if (!opts.tx_hdlc_framer ||
				    descr->hardware)
					opts.tx_hdlc_framer = descr;
			} else if (dynattr->dynattr == opts.amu_compander) {

				struct ks_amu_decompander_descr *descr =
					(struct ks_amu_decompander_descr *)
					dynattr->payload;

				if (!opts.tx_amu_decompander)
					opts.tx_amu_decompander = descr;
			}
		}
	}

	if (opts.enable_tx_hdlc_framer) {
		if (!opts.hdlc_deframer) {
			fprintf(stderr,
				"No HDLC deframer attribute found\n");
			return -ENODEV;
		}

		if (!opts.tx_hdlc_framer) {
			fprintf(stderr,
				"No HDLC deframer along the pipeline\n");
			return -ENODEV;
		}

		opts.tx_hdlc_framer->enabled = TRUE;
	}

	if (opts.enable_tx_octet_reverser) {
		if (!opts.octet_reverser) {
			fprintf(stderr, "No octet reverser attribute found\n");
			return -ENODEV;
		}

		if (!opts.tx_octet_reverser) {
			fprintf(stderr,
				"No octet reverser along the pipeline\n");
			return -ENODEV;
		}

		opts.tx_octet_reverser->enabled = TRUE;
	}


	if (opts.enable_tx_amu_decompander) {
		if (!opts.amu_compander) {
			fprintf(stderr, "No amu_decompander attribute found\n");
			return -ENODEV;
		}

		if (!opts.tx_amu_decompander) {
			fprintf(stderr,
				"No amu_decompander along the pipeline\n");
			return -ENODEV;
		}

		opts.tx_amu_decompander->enabled = TRUE;
		opts.tx_amu_decompander->mu_mode = opts.tx_amu_decompander_mu_mode;
	}

	return 0;
}

static void ks_report_func(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	switch(level) {
//	case KS_LOG_DEBUG:
//        break;

	default:
		vfprintf(stderr, format, ap);
	break;
	}

	va_end(ap);
}

#ifdef DEBUG_CODE
#define debug(format, arg...)		\
	if (opts.debug)			\
		fprintf(stderr,		\
			format,		\
			## arg)

#else
#define debug(format, arg...) do {} while (0)
#endif


BOOL have_to_exit = FALSE;

static void signal_handler(int signal)
{
	have_to_exit = TRUE;
}

int main(int argc, char *argv[])
{
	int err;

	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGQUIT, signal_handler);

	setvbuf(stdout, (char *)NULL, _IONBF, 0);
	setvbuf(stderr, (char *)NULL, _IONBF, 0);

	struct option options[] = {
		{ "debug", no_argument, 0, 0 },
		{ "debug-state", no_argument, 0, 0 },
		{ "debug-netlink", no_argument, 0, 0 },
		{ "debug-router", no_argument, 0, 0 },
		{ "read-to-stdout", optional_argument, 0, 0 },
		{ "read-to-file", required_argument, 0, 0 },
		{ "write-from", required_argument, 0, 0 },
		{ "framed", no_argument, 0, 0 },
		{ "rx-hdlc-deframer", no_argument, 0, 0 },
		{ "rx-octet-reverser", no_argument, 0, 0 },
		{ "tx-hdlc-deframer", no_argument, 0, 0 },
		{ "tx-octet-reverser", no_argument, 0, 0 },
		{ "rx-compander", required_argument, 0, 0 },
		{ "tx-compander", required_argument, 0, 0 },
		{ }
	};

	int c;
	int optidx;

	for(;;) {
		struct option no_opt ={ "", no_argument, 0, 0 };
		struct option *opt;

		c = getopt_long(argc, argv, "", options,
			&optidx);

		if (c == -1)
			break;

		opt = c ? &no_opt : &options[optidx];

		if (!strcmp(opt->name, "debug"))
			opts.debug = TRUE;
		else if (!strcmp(opt->name, "debug-state"))
			opts.debug_state = TRUE;
		else if (!strcmp(opt->name, "debug-netlink"))
			opts.debug_netlink = TRUE;
		else if (!strcmp(opt->name, "debug-router"))
			opts.debug_router = TRUE;
		else if (!strcmp(opt->name, "read-to-stdout"))
			opts.read_to_stdout = TRUE;
		else if (!strcmp(opt->name, "read-to-stdout-binary")) {
			opts.read_to_stdout_binary = TRUE;
		} else if (!strcmp(opt->name, "read-to-file")) {
			opts.read_to_file = optarg;
		} else if (!strcmp(opt->name, "write-from")) {
			if (!strcmp(optarg, "stdin"))
				opts.write_from = WRITE_FROM_STDIN;
			else if (!strcmp(optarg, "loopback"))
				opts.write_from = WRITE_FROM_LOOPBACK;
			else {
				opts.write_from = WRITE_FROM_FILE;
				opts.write_from_file = optarg;
			}
		} else if (!strcmp(opt->name, "framed"))
			opts.framed = TRUE;
		else if (!strcmp(opt->name, "rx-hdlc-deframer"))
			opts.enable_rx_hdlc_deframer = TRUE;
		else if (!strcmp(opt->name, "rx-octet-reverser"))
			opts.enable_rx_octet_reverser = TRUE;
		else if (!strcmp(opt->name, "tx-hdlc-framer"))
			opts.enable_tx_hdlc_framer = TRUE;
		else if (!strcmp(opt->name, "rx-octet-reverser"))
			opts.enable_tx_octet_reverser = TRUE;
		else if (!strcmp(opt->name, "rx-compander")) {
			opts.enable_rx_amu_compander = TRUE;

			if (!strcasecmp(optarg, "alaw"))
				opts.rx_amu_compander_mu_mode = FALSE;
			else if (!strcasecmp(optarg, "mulaw"))
				opts.rx_amu_compander_mu_mode = TRUE;
			else {
				fprintf(stderr, "Unrecognized mode %s\n", optarg);
				print_usage(argv[0]);
				return 1;
			}
		} else if (!strcmp(opt->name, "tx-compander")) {
			opts.enable_tx_amu_decompander = TRUE;

			if (!strcasecmp(optarg, "alaw"))
				opts.tx_amu_decompander_mu_mode = FALSE;
			else if (!strcasecmp(optarg, "mulaw"))
				opts.tx_amu_decompander_mu_mode = TRUE;
			else {
				fprintf(stderr, "Unrecognized mode %s\n", optarg);
				print_usage(argv[0]);
				return 1;
			}
		} else {
			if (c)
				fprintf(stderr,"Unknow option -%c\n", c);
			else
				fprintf(stderr, "Unknow option %s\n",
					opt->name);

			print_usage(argv[0]);
			return 1;
		}
	}

	if (optind < argc)
		opts.node_name = argv[optind];

	int mode;

	if ((opts.read_to_stdout ||
	     opts.read_to_file) &&
	     opts.write_from != WRITE_FROM_NONE) {
		mode = O_RDWR;
	} else if (opts.read_to_stdout ||
		   opts.read_to_file)
		mode = O_RDONLY;
	else if (opts.write_from != WRITE_FROM_NONE)
		mode = O_WRONLY;
	else {
		print_usage(argv[0]);
		return 1;
	}

	debug("Opening userport...\n");

	mode |= O_NONBLOCK;

	int up_fd;
	if (opts.framed)
		up_fd = open("/dev/ks/userport_frame", mode);
	else
		up_fd = open("/dev/ks/userport_stream", mode);

	if (up_fd < 0) {
		perror("cannot open /dev/ks/userport_stream");
		return 1;
	}

	if (fcntl(0, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl(0, O_NONBLOCK)");
		return 1;
	}

	if (fcntl(1, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl(1, O_NONBLOCK)");
		return 1;
	}

	__u32 node_up_id;
	if (ioctl(up_fd, KS_UP_GET_NODEID, (caddr_t)&node_up_id) < 0) {
		perror("ioctl(KS_UP_GET_NODEID)");
		return 1;
	}

	debug("Connecting to kstreamer...\n");

	struct ks_conn *conn;
	conn = ks_conn_create();
	if (!conn) {
		fprintf(stderr, "Cannot initialize kstreamer library\n");
		return 1;
	}

	conn->report_func = ks_report_func;
	conn->debug_state = opts.debug_state;
	conn->debug_netlink = opts.debug_netlink;
	conn->debug_router = opts.debug_router;

	debug("Connecting to kstreamer...\n");
	err = ks_conn_establish(conn);
	if (err < 0) {
		fprintf(stderr, "Cannot connect kstreamer library\n");
		return 1;
	}

	debug("Updating topology...\n");

	ks_update_topology(conn);

	opts.hdlc_framer = ks_dynattr_get_by_name(conn, "hdlc_framer");
	opts.hdlc_deframer = ks_dynattr_get_by_name(conn, "hdlc_deframer");
	opts.octet_reverser = ks_dynattr_get_by_name(conn, "octet_reverser");
	opts.amu_compander = ks_dynattr_get_by_name(conn, "amu_compander");

	struct ks_node *node_up;
	node_up = ks_node_get_by_id(conn, node_up_id);
	if (!node_up) {
		fprintf(stderr, "Cannot find UP node\n");
		return 1;
	}

	debug("node_up: 0x%08x\n", node_up->id);

	struct ks_node *node = NULL;

	if (opts.node_name) {
		if (!strncmp(opts.node_name, "/", 1)) {
			node = ks_node_get_by_path(conn, opts.node_name);
		} else {
			node = ks_node_get_by_id(conn, atoi(opts.node_name));
		}

		if (!node) {
			fprintf(stderr,
				"Cannot find node '%s'\n", opts.node_name);
			return 1;
		}
	}

	debug("node: 0x%08x\n", node->id);

	struct ks_pipeline *rx_pipeline = NULL;
	if (node &&
	    (opts.read_to_stdout ||
	     opts.read_to_file)) {

		debug("Creating rx_pipeline...\n");

		mode = O_RDWR;
		rx_pipeline = ks_pipeline_alloc();
		if (!rx_pipeline) {
			fprintf(stderr,
				"Cannot alloc in pipeline\n");
			return 1;
		}

		err = ks_pipeline_autoroute(rx_pipeline, conn, node, node_up);
		if (err < 0) {
			fprintf(stderr,
				"Cannot connect nodes: %s\n", strerror(-err));
			return 1;
		}

		rx_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;

		err = ks_pipeline_create(rx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr,
				"Cannot create pipeline: %s\n",
					strerror(-err));
			return 1;
		}

		debug("Configuring rx_pipeline...\n");

		err = configure_rx_pipeline(rx_pipeline);
		if (err < 0)
			return 1;

		debug("Configuring rx_pipeline channels...\n");

		ks_pipeline_update_chans(rx_pipeline, conn);

		debug("Starting rx_pipeline...\n");

		rx_pipeline->status = KS_PIPELINE_STATUS_FLOWING;
		err = ks_pipeline_update(rx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot start the pipeline\n");
			return 1;
		}
	}

	struct ks_pipeline *tx_pipeline = NULL;
	if (node && opts.write_from != WRITE_FROM_NONE) {

		debug("Creating tx_pipeline...\n");

		tx_pipeline = ks_pipeline_alloc();
		if (!tx_pipeline) {
			fprintf(stderr,
				"Cannot alloc in pipeline\n");
			return 1;
		}

		err = ks_pipeline_autoroute(tx_pipeline, conn, node_up, node);
		if (err < 0) {
			fprintf(stderr,
				"Cannot connect nodes: %s\n", strerror(-err));
			return 1;
		}

		tx_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;
		err = ks_pipeline_create(tx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr,
				"Cannot create pipeline: %s\n",
					strerror(-err));
			return -err;
		}

		debug("Configuring tx_pipeline channels...\n");

		configure_tx_pipeline(tx_pipeline);

		debug("Configuring tx_pipeline...\n");
		ks_pipeline_update_chans(tx_pipeline, conn);

		debug("Starting tx_pipeline...\n");

		tx_pipeline->status = KS_PIPELINE_STATUS_FLOWING;
		err = ks_pipeline_update(tx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot start the pipeline\n");
			return 1;
		}
	}

	debug("Entering main cycle...\n");

	char buf[4096];
	struct pollfd pollfd = { up_fd, POLLIN, 0 };

	while(!have_to_exit) {
		if (poll(&pollfd, 1, -1) < 0) {
			if (errno == EINTR)
				continue;

			perror("poll");
			return 1;
		}

		int nread = 0;

		if (rx_pipeline) {
			nread = read(up_fd, buf, sizeof(buf));

			if (opts.read_to_stdout_binary)
				write(1, buf, nread);
			else {
				printf(" %d: ", nread);

				int i;
				for (i=0; i<min(8, nread); i++)
					printf("%02x", *(__u8 *)(buf + i));

				printf("\n");
			}
		}

		if (opts.write_from == WRITE_FROM_STDIN) {
			nread = read(0, buf, 160);

			int nwrote = 0;
			if (nread > 0)
				nwrote = write(up_fd, buf, nread);

			printf("Nwrote = %d\n", nwrote);
		} else if (opts.write_from == WRITE_FROM_FILE) {

		} else if (opts.write_from == WRITE_FROM_LOOPBACK) {

			if (nread > 0)
				write(up_fd, buf, nread);
		}
	}

#if 0
	int i;
	while(1) {
		memset(buf, 0x55, 160);
		printf("%d\n", write(up_fd, buf, 160));

		usleep(20000);
	}
#endif

	
	srand(time(NULL)*getpid());
	usleep(rand() % 250000);

	if (rx_pipeline) {
		debug("Stopping rx_pipeline...\n");

		rx_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;
		err = ks_pipeline_update(rx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot stop the pipeline\n");
			return 1;
		}

		debug("Destroying rx_pipeline...\n");

		err = ks_pipeline_destroy(rx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot destroy the pipeline\n");
			return 1;
		}
	}

	if (tx_pipeline) {
		debug("Stopping tx_pipeline...\n");

		tx_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;
		err = ks_pipeline_update(tx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot stop the pipeline\n");
			return 1;
		}

		debug("Destroying tx_pipeline...\n");

		err = ks_pipeline_destroy(tx_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot destroy the pipeline\n");
			return 1;
		}
	}

	debug("Closing connection to kstreamer...\n");

	ks_conn_destroy(conn);

	return 0;
}

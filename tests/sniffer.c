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

/*#include <linux/kstreamer/router.h>
#include <linux/kstreamer/dynattr.h>
#include <linux/kstreamer/chan.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/netlink.h>*/
#include <linux/kstreamer/userport.h>

#include <linux/kstreamer/hdlc_framer.h>
#include <linux/kstreamer/octet_reverser.h>

#include <list.h>

#include <libskb.h>
#include <libkstreamer.h>

#define min(a,b) ((a) > (b) ? (b) : (a))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct opts
{
	int in;
	BOOL in_reverser;
	BOOL in_framer;

	int out;
} opts;

int main(int argc, char *argv[])
{
	int err;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);
	setvbuf(stderr, (char *)NULL, _IONBF, 0);

	opts.in = 1;
	opts.out = 0;


	int mode;

	if (opts.in && opts.out)
		mode = O_RDWR;
	else if (opts.in)
		mode = O_RDONLY;
	else if (opts.out)
		mode = O_WRONLY;
	else
		assert(0);

	int up_fd;
	up_fd = open("/dev/ks/userport_stream", mode);
	//up_fd = open("/dev/ks/userport_frame", mode);
	if (up_fd < 0) {
		perror("cannot open /dev/ks/userport_stream");
		return 1;
	}

	__u32 node_up_id;
	if (ioctl(up_fd, KS_UP_GET_NODEID, (caddr_t)&node_up_id) < 0) {
		perror("ioctl(KS_UP_GET_NODEID)");
		return 1;
	}

	struct ks_conn *conn;
	conn = ks_conn_create();
	if (!conn) {
		fprintf(stderr, "Cannot initialize kstreamer library\n");
		return 1;
	}

	ks_update_topology(conn);

	struct ks_node *node_up;
	node_up = ks_node_get_by_id(node_up_id);
	if (!node_up) {
		fprintf(stderr, "Cannot find UP node\n");
		return 1;
	}

	struct ks_node *node;

	if (!strncmp(argv[1], "/sys/", 5)) {
		char *path;

		path = realpath(argv[1], NULL);
		if (!path) {
			fprintf(stderr, "Cannot allocate real path\n");
			return 1;
		}

		node = ks_node_get_by_path(path + 4);

		free(path);
	} else {
		node = ks_node_get_by_id(atoi(argv[1]));
	}

	if (!node) {
		fprintf(stderr, "Cannot find node '%s'\n", argv[1]);
		return 1;
	}

	struct ks_dynattr *hdlc_framer;
	hdlc_framer = ks_dynattr_get_by_name("hdlc_framer");
	if (!hdlc_framer) {
		fprintf(stderr, "No HDLC framer attribute found\n");
		return 1;
	}

	struct ks_dynattr *octet_reverser;
	octet_reverser = ks_dynattr_get_by_name("octet_reverser");
	if (!octet_reverser) {
		fprintf(stderr, "No octet reverser attribute found\n");
		return 1;
	}

	struct ks_pipeline *in_pipeline;
	struct ks_pipeline *out_pipeline;

	if (opts.in) {
		in_pipeline = ks_connect(conn, node, node_up, &err);
		if (!in_pipeline) {
			fprintf(stderr,
				"Cannot connect nodes: %s\n", strerror(-err));
			return 1;
		}

		in_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;

		err = ks_pipeline_create(in_pipeline, conn);
		if (err < 0) {
			fprintf(stderr,
				"Cannot create pipeline: %s\n",
					strerror(-err));
			return -err;
		}

		int i;
		for(i=0; i<in_pipeline->chans_cnt; i++) {
			struct ks_chan *chan = in_pipeline->chans[i];

			struct ks_dynattr_instance *dynattr;
			list_for_each_entry(dynattr, &chan->dynattrs, node) {
				if (dynattr->dynattr == octet_reverser) {
					struct octet_reverser_descr *descr =
						(struct octet_reverser_descr *)
						dynattr->payload;

//					descr->flags |=
//						OCTET_REVERSER_FLAG_ENABLED;

					descr->flags &=
						~OCTET_REVERSER_FLAG_ENABLED;

				} else if (dynattr->dynattr == hdlc_framer) {
					struct hdlc_framer_descr *descr =
						(struct hdlc_framer_descr *)
						dynattr->payload;

					descr->flags |=
						HDLC_FRAMER_FLAG_ENABLED;

					descr->flags &=
						~HDLC_FRAMER_FLAG_ENABLED;
				}
			}
		}
done:

		ks_pipeline_update_chans(in_pipeline, conn);

		in_pipeline->status = KS_PIPELINE_STATUS_FLOWING;
		err = ks_pipeline_update(in_pipeline, conn);
		if (err < 0) {
			fprintf(stderr, "Cannot start the pipeline\n");
			return 1;
		}
	}

	if (opts.out) {
		out_pipeline = ks_connect(conn, node_up, node, &err);
		if (!out_pipeline) {
			fprintf(stderr,
				"Cannot connect nodes: %s\n", strerror(-err));
			return 1;
		}

		out_pipeline->status = KS_PIPELINE_STATUS_FLOWING;

		err = ks_pipeline_create(out_pipeline, conn);
		if (err < 0) {
			fprintf(stderr,
				"Cannot create pipeline: %s\n",
					strerror(-err));
			return -err;
		}
	}

	char buf[4096];
	struct pollfd pollfd = { up_fd, POLLIN, 0 };

#if 1
	while(1) {
		if (poll(&pollfd, 1, -1) < 0) {
			perror("poll");
			return 1;
		}

		int nread = read(up_fd, buf, sizeof(buf));
		printf(" %d: ", nread);

		int i;
		for (i=0; i<min(8, nread); i++)
			printf("%02x", *(__u8 *)(buf + i));

		printf("\n");
	}
#endif

#if 0
	int i;
	while(1) {
		memset(buf, 0x55, 160);
		printf("%d\n", write(up_fd, buf, 160));

		usleep(20000);
	}
#endif

	sleep(10000);

	in_pipeline->status = KS_PIPELINE_STATUS_CONNECTED;
	err = ks_pipeline_update(in_pipeline, conn);
	if (err < 0) {
		fprintf(stderr, "Cannot stop the pipeline\n");
		return 1;
	}

	err = ks_pipeline_destroy(in_pipeline, conn);
	if (err < 0) {
		fprintf(stderr, "Cannot destroy the pipeline\n");
		return 1;
	}

	ks_conn_destroy(conn);

	return 0;
}

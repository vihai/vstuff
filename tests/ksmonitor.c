/*
 * KStreamer streaming network topology monitor
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
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <getopt.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <linux/types.h>
#include <linux/netlink.h>

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

static void sig_handler(int sig)
{
	have_to_exit = TRUE;
}

static void dynattr_dump_xml(struct ks_dynattr *dynattr)
{
	printf("    <dynattr id=\"%d\">\n", dynattr->id);
	printf("      <name>%s</name>\n", dynattr->name);
	printf("    </dynattr>\n");
}

static void node_dump_xml(struct ks_node *node)
{
	printf("    <node id=\"%d\">\n", node->id);
	printf("      <path>%s</path>\n", node->path);
	printf("    </node>\n");
}

static void chan_dump_xml(struct ks_chan *chan)
{
	printf("    <chan id=\"%d\">\n", chan->id);
	printf("      <path>%s</path>\n", chan->path);
	printf("      <from>%d</from>\n", chan->from->id);
	printf("      <to>%d</to>\n", chan->to->id);

	if (chan->pipeline)
		printf("      <pipeline>%d</pipeline>\n",
			chan->pipeline->id);

	struct ks_dynattr_instance *dynattr;
	list_for_each_entry(dynattr, &chan->dynattrs, node) {
		printf("      <dynattr id=\"%d\">\n",
			dynattr->dynattr->id);
		printf("        <name>%s</name>\n",
			dynattr->dynattr->name);

		char *raw = alloca(dynattr->len * 2 + 1);
		int i;
		for(i=0; i<dynattr->len; i++) {
			sprintf(raw + i * 2, "%02x",
				*(__u8 *)(dynattr->payload + i));
		}

		printf("        <raw>%s</raw>\n", raw);
		printf("      </dynattr>\n");
	}

	printf("    </chan>\n");
}

static void pipeline_dump_xml(
	struct ks_pipeline *pipeline)
{
	printf("    <pipeline id=\"%d\">\n", pipeline->id);
	printf("      <path>%s</path>\n", pipeline->path);
	printf("      <status>%d</status>\n", pipeline->status);

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		printf("      <chan_id>%d</chan_id>\n",
			pipeline->chans[i]->id);
	}

	printf("    </pipeline>\n");
}

static void topology_event_handler_xml(
	struct ks_conn *conn,
	int message_type,
	void *object)
{

	/* FIXME! TODO! String are not properly escaped!!! */

	switch(message_type) {

	case KS_NETLINK_DYNATTR_NEW: {
		struct ks_dynattr *dynattr = object;

		printf("<message type=\"new_object\">\n");
		printf("  <object>\n");
		dynattr_dump_xml(dynattr);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_DYNATTR_DEL: {
		struct ks_dynattr *dynattr = object;

		printf("<message type=\"del_object\">\n");
		printf("  <object>\n");
		dynattr_dump_xml(dynattr);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_DYNATTR_SET: {
		struct ks_dynattr *dynattr = object;

		printf("<message type=\"set_object\">\n");
		printf("  <object>\n");
		dynattr_dump_xml(dynattr);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_NODE_NEW: {
		struct ks_node *node = object;

		printf("<message type=\"new_object\">\n");
		printf("  <object>\n");
		node_dump_xml(node);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_NODE_DEL: {
		struct ks_node *node = object;

		printf("<message type=\"del_object\">\n");
		printf("  <object>\n");
		node_dump_xml(node);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_NODE_SET: {
		struct ks_node *node = object;

		printf("<message type=\"set_object\">\n");
		printf("  <object>\n");
		node_dump_xml(node);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_CHAN_NEW: {
		struct ks_chan *chan = object;

		printf("<message type=\"new_object\">\n");
		printf("  <object>\n");
		chan_dump_xml(chan);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_CHAN_DEL: {
		struct ks_chan *chan = object;

		printf("<message type=\"del_object\">\n");
		printf("  <object>\n");
		chan_dump_xml(chan);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_CHAN_SET: {
		struct ks_chan *chan = object;

		printf("<message type=\"set_object\">\n");
		printf("  <object>\n");
		chan_dump_xml(chan);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_PIPELINE_NEW: {
		struct ks_pipeline *pipeline = object;

		printf("<message type=\"new_object\">\n");
		printf("  <object>\n");
		pipeline_dump_xml(pipeline);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_PIPELINE_DEL: {
		struct ks_pipeline *pipeline = object;

		printf("<message type=\"del_object\">\n");
		printf("  <object>\n");
		pipeline_dump_xml(pipeline);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_PIPELINE_SET: {
		struct ks_pipeline *pipeline = object;

		printf("<message type=\"set_object\">\n");
		printf("  <object>\n");
		pipeline_dump_xml(pipeline);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;
	}
}

int main(int argc, char *argv[])
{
	int err;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);
	setvbuf(stderr, (char *)NULL, _IONBF, 0);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGQUIT, sig_handler);

	struct ks_conn *conn;
	conn = ks_conn_create();
	if (!conn) {
		fprintf(stderr, "Cannot initialize kstreamer library\n");
		return 1;
	}

	//conn->dump_packets = TRUE;

	err = ks_conn_establish(conn);
	if (err < 0) {
		fprintf(stderr, "Cannot connect kstreamer library\n");
		return 1;
	}

	conn->topology_event_callback = topology_event_handler_xml;

	ks_update_topology(conn);

	while(!have_to_exit)
		sleep(3600);

	ks_conn_destroy(conn);

	return 0;
}

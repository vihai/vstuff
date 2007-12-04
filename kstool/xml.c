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

#include <libkstreamer/libkstreamer.h>

#include "xml.h"

void feature_dump_xml(struct ks_feature *feature)
{
	printf("    <feature id=\"%d\">\n", feature->id);
	printf("      <name>%s</name>\n", feature->name);
	printf("    </feature>\n");
}

void node_dump_xml(struct ks_node *node)
{
	printf("    <node id=\"%d\">\n", node->id);
	printf("      <path>%s</path>\n", node->path);
	printf("    </node>\n");
}

void chan_dump_xml(struct ks_chan *chan)
{
	printf("    <chan id=\"%d\">\n", chan->id);
	printf("      <path>%s</path>\n", chan->path);
	printf("      <from>%d</from>\n", chan->from->id);
	printf("      <to>%d</to>\n", chan->to->id);

	if (chan->pipeline)
		printf("      <pipeline>%d</pipeline>\n",
			chan->pipeline->id);

	struct ks_feature_value *featval;
	list_for_each_entry(featval, &chan->features, node) {
		printf("      <feature id=\"%d\">\n",
			featval->feature->id);
		printf("        <name>%s</name>\n",
			featval->feature->name);

		char *raw = alloca(featval->len * 2 + 1);
		int i;
		for(i=0; i<featval->len; i++) {
			sprintf(raw + i * 2, "%02x",
				*(__u8 *)(featval->payload + i));
		}

		printf("        <raw>%s</raw>\n", raw);
		printf("      </feature>\n");
	}

	printf("    </chan>\n");
}

void pipeline_dump_xml(struct ks_pipeline *pipeline)
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

void topology_event_handler_xml(
	struct ks_conn *conn,
	int message_type,
	void *object)
{

	/* FIXME! TODO! String are not properly escaped!!! */

	switch(message_type) {

	case KS_NETLINK_FEATURE_NEW: {
		struct ks_feature *feature = object;

		printf("<message type=\"new_object\">\n");
		printf("  <object>\n");
		feature_dump_xml(feature);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_FEATURE_DEL: {
		struct ks_feature *feature = object;

		printf("<message type=\"del_object\">\n");
		printf("  <object>\n");
		feature_dump_xml(feature);
		printf("  </object>\n");
		printf("</message>\n");
	}
	break;

	case KS_NETLINK_FEATURE_SET: {
		struct ks_feature *feature = object;

		printf("<message type=\"set_object\">\n");
		printf("  <object>\n");
		feature_dump_xml(feature);
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

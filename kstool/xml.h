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

#ifndef _XML_H
#define _XML_H

void dynattr_dump_xml(struct ks_dynattr *dynattr);
void node_dump_xml(struct ks_node *node);
void chan_dump_xml(struct ks_chan *chan);
void pipeline_dump_xml(struct ks_pipeline *pipeline);
void topology_event_handler_xml(
	struct ks_conn *conn,
	int message_type,
	void *object);

#endif

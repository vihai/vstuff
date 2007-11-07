/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBKSTREAMER_CONN_H
#define _LIBKSTREAMER_CONN_H

#include <linux/types.h>
#include <list.h>

#include <linux/kstreamer/netlink.h>

#include <pthread.h>

#include "dynattr.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"

enum ks_conn_message_type
{
	KS_CONN_MSG_CLOSE,
	KS_CONN_MSG_REFRESH,
};

struct ks_conn_message
{
	enum ks_conn_message_type type;
	
	int len;
	__u8 data[];
};

enum ks_topology_state
{
	KS_TOPOLOGY_STATE_NULL,
	KS_TOPOLOGY_STATE_SYNCING,
	KS_TOPOLOGY_STATE_SYNCHED,
};

enum ks_conn_state
{
	KS_CONN_STATE_NULL,
	KS_CONN_STATE_IDLE,
	KS_CONN_STATE_WAITING_ACK,
	KS_CONN_STATE_WAITING_DONE,
};

#define DYNATTR_HASHBITS 8
#define DYNATTR_HASHSIZE ((1 << DYNATTR_HASHBITS) - 1)

#define PIPELINE_HASHBITS 8
#define PIPELINE_HASHSIZE ((1 << PIPELINE_HASHBITS) - 1)

#define CHAN_HASHBITS 8
#define CHAN_HASHSIZE ((1 << CHAN_HASHBITS) - 1)

#define NODE_HASHBITS 8
#define NODE_HASHSIZE ((1 << NODE_HASHBITS) - 1)

struct ks_conn
{
	pthread_mutex_t topology_lock;

	enum ks_topology_state topology_state;

	enum ks_conn_state state;

	struct hlist_head dynattrs_hash[DYNATTR_HASHSIZE];
	struct hlist_head chans_hash[CHAN_HASHSIZE];
	struct hlist_head nodes_hash[NODE_HASHSIZE];
	struct hlist_head pipelines_hash[PIPELINE_HASHSIZE];

	int sock;

	struct ks_netlink_version_response version;

	int seqnum;
	__u32 pid;

	int debug_netlink;
	int debug_router;
	int debug_state;

	pthread_t protocol_thread;
	int cmd_read;
	int cmd_write;

	pthread_mutex_t xacts_lock;
	struct list_head xacts;

	struct ks_xact *cur_xact;
	struct ks_req *cur_req;

	void (*report_func)(int level, const char *format, ...)
		__attribute__ ((format (printf, 2, 3)));

	void (*topology_event_callback)(
		struct ks_conn *conn,
		int message_type,
		void *object);
};

struct ks_req;

struct ks_conn *ks_conn_create(void);
int ks_conn_establish(struct ks_conn *conn);
void ks_conn_destroy(struct ks_conn *conn);

int ks_conn_sync(struct ks_conn *conn);

void ks_conn_send_message(
	struct ks_conn *conn,
	enum ks_conn_message_type mt,
	void *data,
	int len);

#ifdef _LIBKSTREAMER_PRIVATE_

#define report_conn(conn, lvl, format, arg...)				\
	(conn)->report_func((lvl),					\
		"ks: "							\
		format,							\
		## arg)

#define debug_conn(conn, flag, format, arg...)				\
	do {								\
		if ((conn)->flag) {					\
			(conn)->report_func(LOG_DEBUG,			\
				"ks: "					\
				format,					\
				## arg);				\
		}							\
	} while(0)

void ks_conn_add_xact(struct ks_conn *conn, struct ks_xact *xact);

void ks_conn_set_state(
	struct ks_conn *conn,
	enum ks_conn_state state);

void ks_conn_topology_updated(
	struct ks_conn *conn,
	int message_type,
	void *object);

#endif

#endif

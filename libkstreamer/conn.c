/*
 * Userland Kstreamer Helper Routines
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#define _GNU_SOURCE
#define _LIBKSTREAMER_PRIVATE_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/kstreamer/netlink.h>

#include "libkstreamer.h"
#include "conn.h"
#include "xact.h"
#include "req.h"
#include "util.h"

void ks_conn_add_xact(struct ks_conn *conn, struct ks_xact *xact)
{
	list_add_tail(&ks_xact_get(xact)->node, &conn->xacts);
}

int ks_conn_sync(struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_xact_queue_new_request(xact,
			NLMSG_NOOP,
			NLM_F_REQUEST);

	ks_xact_run(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_request_version;
	}

	ks_req_put(req);
	ks_xact_put(xact);

	return 0;

err_request_version:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

static int ks_conn_get_version(struct ks_conn *conn)
{
	int err;

	struct ks_xact *xact = ks_xact_alloc(conn);
	if (!xact) {
		err = -ENOMEM;
		goto err_xact_alloc;
	}

	struct ks_req *req;
	req = ks_xact_queue_new_request(xact,
			KS_NETLINK_VERSION,
			NLM_F_REQUEST);

	ks_xact_run(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_request_version;
	}

	memcpy(&conn->version, req->response_data, sizeof(conn->version));

	ks_req_put(req);

	ks_xact_put(xact);

	return 0;

err_request_version:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

struct ks_conn *ks_conn_create(void)
{
	struct ks_conn *conn;
	int err;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return NULL;

	memset(conn, 0, sizeof(*conn));

	conn->topology_state = KS_TOPOLOGY_STATE_NULL;
	conn->state = KS_CONN_STATE_NULL;
	conn->dump_packets = TRUE;
	conn->seqnum = 1234;
	INIT_LIST_HEAD(&conn->xacts);
	conn->pid = getpid();

	conn->xact_wait = ks_xact_wait_default;
	conn->req_wait = ks_req_wait_default;

	conn->sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KSTREAMER);
	if (conn->sock < 0) {
		perror("Unable to open kstreamer socket");
		return NULL;
	}

	struct sockaddr_nl bind_sa;
	memset(&bind_sa, 0, sizeof(bind_sa));
	bind_sa.nl_family = AF_NETLINK;
	bind_sa.nl_pid = getpid();
	bind_sa.nl_groups = KS_NETLINK_GROUP_TOPOLOGY;

	if (bind(conn->sock, (struct sockaddr *)&bind_sa,
						sizeof(bind_sa)) < 0) {
		perror("Unable to bind netlink socket");
		return NULL;
	}

	err = ks_conn_get_version(conn);
	if (err < 0) {
		fprintf(stderr, "Cannot get version: %s\n", strerror(err));
		return NULL;
	}

	if (conn->version.major > KS_LIB_VERSION_MAJOR) {
		fprintf(stderr, "Unsupported kstreamer interface version"
				" %u.%u.%u\n",
				conn->version.major,
				conn->version.minor,
				conn->version.service);
		return NULL;
	}

	return conn;
}

void ks_conn_destroy(struct ks_conn *conn)
{
	close(conn->sock);

	free(conn);
}

static const char *ks_conn_state_to_text(enum ks_conn_state state)
{
	switch(state) {
	case KS_CONN_STATE_NULL:
		return "NULL";
	case KS_CONN_STATE_IDLE:
		return "IDLE";
	case KS_CONN_STATE_WAITING_ACK:
		return "WAITING_ACK";
	case KS_CONN_STATE_WAITING_DONE:
		return "WAITING_DONE";
	}

	return "*INVALID*";
}

void ks_conn_set_state(
	struct ks_conn *conn,
	enum ks_conn_state state)
{
	fprintf(stderr, "Conn state changed from %s to %s\n",
		ks_conn_state_to_text(conn->state),
		ks_conn_state_to_text(state));

	conn->state = state;
}


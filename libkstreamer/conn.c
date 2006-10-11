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

#include <linux/netlink.h>
#include <linux/kstreamer/netlink.h>

#include "conn.h"
#include "request.h"
#include "util.h"

void ks_conn_add_request(struct ks_conn *conn, struct ks_request *req)
{
	list_add_tail(&ks_request_get(req)->node, &conn->requests);
}

struct ks_conn *ks_conn_create(void)
{
	struct ks_conn *conn;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return NULL;

	memset(conn, 0, sizeof(conn));

	conn->state = KS_STATE_NULL;
	conn->dump_packets = TRUE;
	conn->seqnum = 1234;
	INIT_LIST_HEAD(&conn->requests);

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
	
	return conn;
}

void ks_conn_destroy(struct ks_conn *conn)
{
	close(conn->sock);

	free(conn);
}


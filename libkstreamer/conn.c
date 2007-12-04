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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/kstreamer/netlink.h>

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/conn.h>
#include <libkstreamer/xact.h>
#include <libkstreamer/req.h>
#include <libkstreamer/util.h>
#include <libkstreamer/logging.h>

void ks_conn_add_xact(struct ks_conn *conn, struct ks_xact *xact)
{
	pthread_mutex_lock(&conn->xacts_lock);
	list_add_tail(&ks_xact_get(xact)->node, &conn->xacts);
	pthread_mutex_unlock(&conn->xacts_lock);
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

	ks_xact_submit(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_request_noop;
	}

	ks_req_put(req);
	ks_xact_put(xact);

	return 0;

err_request_noop:
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

	ks_xact_submit(xact);
	ks_req_wait(req);

	if (req->err < 0) {
		err = req->err;
		ks_req_put(req);
		goto err_request_version;
	}

	memcpy(&conn->version, req->response_payload,
		sizeof(conn->version));

	ks_req_put(req);

	ks_xact_put(xact);

	return 0;

err_request_version:
	ks_xact_put(xact);
err_xact_alloc:

	return err;
}

static void ks_report_default(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	switch(level) {
	case KS_LOG_DEBUG:
        break;

	default:
		vfprintf(stderr, format, ap);
	break;
	}

	va_end(ap);
}

void ks_conn_topology_updated(
	struct ks_conn *conn,
	int message_type,
	void *object)
{
	if (conn->topology_event_callback)
		conn->topology_event_callback(conn, message_type, object);
}

struct ks_conn *ks_conn_create(void)
{
	struct ks_conn *conn;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return NULL;

	memset(conn, 0, sizeof(*conn));

	conn->topology_state = KS_TOPOLOGY_STATE_NULL;
	conn->state = KS_CONN_STATE_NULL;

	conn->debug_netlink = FALSE;
	conn->debug_router = FALSE;
	conn->debug_state = FALSE;

	conn->seqnum = 1234;
	INIT_LIST_HEAD(&conn->xacts);
	conn->pid = getpid();

	pthread_mutex_init(&conn->refcnt_lock, NULL);
	pthread_rwlock_init(&conn->topology_lock, NULL);
	pthread_mutex_init(&conn->xacts_lock, NULL);

	conn->report_func = ks_report_default;

	return conn;
}

static void ks_xact_flush(struct ks_xact *xact)
{
	if (xact->out_skb) {
		if (xact->out_skb->len) {
			ks_netlink_sendmsg(xact->conn, xact->out_skb);
			xact->out_skb = NULL;
		}
	}
}

static int ks_conn_send_next_packet(struct ks_conn *conn)
{
	pthread_mutex_lock(&conn->xacts_lock);
	if (!conn->cur_xact) {
		if (list_empty(&conn->xacts)) {
			pthread_mutex_unlock(&conn->xacts_lock);

			return 0;
		}

		conn->cur_xact = list_entry(conn->xacts.next, struct ks_xact,
									node);
		list_del(&conn->cur_xact->node);
	}
	pthread_mutex_unlock(&conn->xacts_lock);

	struct ks_xact *xact = conn->cur_xact;

	if (list_empty(&xact->requests))
		return 0;

	if (!list_empty(&xact->requests_sent))
		return 0;

	struct ks_req *req;
	/*list_for_each_entry(req, &xact->requests, node)
		size += NLMSG_ALIGN(NLMSG_LENGTH(0));*/

	struct ks_req *t;
	list_for_each_entry_safe(req, t, &xact->requests, node) {
retry:
		ks_xact_need_skb(xact);
		if (!xact->out_skb)
			return -ENOMEM;

		void *oldtail = xact->out_skb->tail;

		struct nlmsghdr *nlh;
		nlh = ks_nlmsg_put(xact->out_skb, xact->conn->pid, req->id,
						req->type, req->flags, 0);
		if (!nlh) {
			ks_xact_flush(xact);
			goto retry;
		}

		if (req->skb) {
			ks_xact_need_skb(xact);
			if (!xact->out_skb)
				return -ENOMEM;

			if (req->skb->len >= skb_tailroom(xact->out_skb)) {
				skb_trim(xact->out_skb,
					oldtail - xact->out_skb->data);
				ks_xact_flush(xact);
				goto retry;
			}

			memcpy(skb_put(xact->out_skb, req->skb->len),
				req->skb->data, req->skb->len); 
		}

		nlh->nlmsg_len = xact->out_skb->tail - oldtail;

		pthread_mutex_lock(&xact->requests_lock);
		list_del(&req->node);
		list_add_tail(&req->node, &xact->requests_sent);
		pthread_mutex_unlock(&xact->requests_lock);
	}

	ks_xact_flush(xact);

	ks_conn_set_state(conn, KS_CONN_STATE_WAITING_ACK);

	return 0;
}

void ks_conn_send_message(
	struct ks_conn *conn,
	enum ks_conn_message_type mt,
	void *data,
	int len)
{
	struct ks_conn_message *msg;

	msg = alloca(sizeof(*msg) + len);
	msg->type = mt;
	msg->len = len;

	if (data && len)
		memcpy(&msg->data, data, len);

	if (write(conn->cmd_write, msg, sizeof(*msg) + len) < 0) {
		report_conn(conn, LOG_WARNING,
			"Error writing to command pipe: %s\n",
				strerror(errno));
	}
}

const char *ks_conn_message_type_to_text(
        enum ks_conn_message_type mt)
{
	switch(mt) {
	case KS_CONN_MSG_CLOSE:
		return "CLOSE";
	case KS_CONN_MSG_REFRESH:
		return "REFRESH";
	}

	return "*UNKNOWN*";
}

static int ks_conn_receive_message(
	struct ks_conn *conn,
	struct pollfd polls[])
{
	struct ks_conn_message msgh;

	if (read(conn->cmd_read, &msgh, sizeof(msgh)) < 0) {
		report_conn(conn, LOG_WARNING,
			"Error reading from command pipe: %s\n",
			strerror(errno));
		return FALSE;
	}

	struct ks_conn_message *msg;

	if (msgh.len) {
		msg = alloca(sizeof(*msg) + msgh.len);
		if (!msg) {
			assert(0);
			// FIXME
		}

		memcpy(msg, &msgh, sizeof(*msg));

		read(conn->cmd_read, &msg->data, msg->len);
	} else
		msg = &msgh;

//	ks_conn_debug_messages(conn, "Received message %s (len=%d)\n",
//		ks_conn_message_type_to_text(msg->type), msg->len);

	if (msg->type == KS_CONN_MSG_CLOSE) {
		return TRUE;
	} else if (msg->type == KS_CONN_MSG_REFRESH)
		return FALSE;

	return FALSE;
}

void *ks_protocol_thread_main(void *data)
{
	struct ks_conn *conn = data;

	struct pollfd pollfds[2];

	pollfds[0].fd = conn->cmd_read;
	pollfds[0].events = POLLHUP | POLLERR | POLLIN;

	pollfds[1].fd = conn->sock;
	pollfds[1].events = POLLHUP | POLLERR | POLLIN;

	conn->state = KS_CONN_STATE_IDLE;

	for(;;) {
		int res;
		res = poll(pollfds, ARRAY_SIZE(pollfds), -1);
		if (res < 0) {
			if (errno != EINTR) {
				report_conn(conn, LOG_WARNING,
					"kstreamer: Error polling: %s\n",
					strerror(errno));
			}

			continue;
		}

		if (pollfds[0].revents & POLLHUP ||
		    pollfds[0].revents & POLLERR ||
		    pollfds[0].revents & POLLNVAL ||
		    pollfds[0].revents & POLLIN) {

			if (ks_conn_receive_message(conn, pollfds))
				break;

			if (conn->state == KS_CONN_STATE_IDLE)
				ks_conn_send_next_packet(conn);
		}

		if (pollfds[1].revents & POLLHUP ||
		    pollfds[1].revents & POLLERR ||
		    pollfds[1].revents & POLLNVAL ||
		    pollfds[1].revents & POLLIN) {
			if (ks_netlink_receive(conn) < 0)
				break;
		}
	}

	return NULL;
}

int ks_conn_establish(struct ks_conn *conn)
{
	int err;

	int filedes[2];
	if (pipe(filedes) < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot create cmd pipe: %s\n",
			strerror(errno));
		err = -errno;
		goto err_pipe;
	}

#if 0
	if (fcntl(filedes[0], F_SETFL, O_NONBLOCK) < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot set pipe to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_fcntl_0;
	}

	if (fcntl(filedes[1], F_SETFL, O_NONBLOCK) < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot set pipe to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_fcntl_1;
	}
#endif

	conn->cmd_read = filedes[0];
	conn->cmd_write = filedes[1];

	conn->sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KSTREAMER);
	if (conn->sock < 0) {
		perror("Unable to open kstreamer socket");
		err = -errno;
		goto err_socket;
	}

	struct sockaddr_nl bind_sa;
	memset(&bind_sa, 0, sizeof(bind_sa));
	bind_sa.nl_family = AF_NETLINK;
	bind_sa.nl_pid = getpid();
	bind_sa.nl_groups = KS_NETLINK_GROUP_TOPOLOGY;

	if (bind(conn->sock, (struct sockaddr *)&bind_sa,
						sizeof(bind_sa)) < 0) {
		perror("Unable to bind netlink socket");
		err = -errno;
		goto err_bind;
	}

	pthread_attr_t attr;
	err = pthread_attr_init(&attr);
	if (err < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot create thread attributes: %s\n",
			strerror(errno));
		goto err_pthread_attr_init;
	}

	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	err = pthread_create(&conn->protocol_thread, &attr,
				ks_protocol_thread_main, conn);
	if (err < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot create thread: %s\n",
			strerror(errno));
		goto err_pthread_create;
	}

	err = ks_conn_get_version(conn);
	if (err < 0) {
		report_conn(conn, LOG_ERR,
			"Cannot get version: %s\n", strerror(-err));
		goto err_get_version;
	}

	if (conn->version.major > KS_LIB_VERSION_MAJOR) {
		report_conn(conn, LOG_ERR,
			"Unsupported kstreamer interface version"
			" %u.%u.%u\n",
			conn->version.major,
			conn->version.minor,
			conn->version.service);
		err = -EINVAL;
		goto err_invalid_version;
	}

	return 0;

err_invalid_version:
err_get_version:
	pthread_join(conn->protocol_thread, NULL);
err_pthread_create:
	pthread_attr_destroy(&attr);
err_pthread_attr_init:
err_bind:
	close(conn->sock);
err_socket:
//err_fcntl_1:
//err_fcntl_0:
	close(conn->cmd_read);
	close(conn->cmd_write);
err_pipe:

	return err;
}

void ks_conn_destroy(struct ks_conn *conn)
{
	ks_conn_send_message(conn, KS_CONN_MSG_CLOSE, NULL, 0);

	int err;
	err = -pthread_join(conn->protocol_thread, NULL);
	if (err) {
		report_conn(conn, LOG_ERR,
			"Cannot join protocol thread: %s\n",
			strerror(-err));
	}

	close(conn->cmd_read);
	close(conn->cmd_write);

	pthread_mutex_destroy(&conn->xacts_lock);
	pthread_rwlock_destroy(&conn->topology_lock);
	pthread_mutex_destroy(&conn->refcnt_lock);

	close(conn->sock);

	ks_pipeline_flush(conn);
	ks_chan_flush(conn);
	ks_node_flush(conn);
	ks_feature_flush(conn);

	free(conn);
}

static const char *ks_topology_state_to_text(enum ks_topology_state state)
{
	switch(state) {
	case KS_TOPOLOGY_STATE_NULL:
		return "NULL";
	case KS_TOPOLOGY_STATE_SYNCING:
		return "SYNCING";
	case KS_TOPOLOGY_STATE_SYNCHED:
		return "SYNCHED";
	}

	return "*INVALID*";
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
	debug_conn(conn, debug_state,
			"Conn state changed from %s to %s\n",
			ks_conn_state_to_text(conn->state),
			ks_conn_state_to_text(state));

	conn->state = state;
}

void ks_conn_set_topology_state(
	struct ks_conn *conn,
	enum ks_topology_state state)
{
	debug_conn(conn, debug_state,
			"Topology state changed from %s to %s\n",
			ks_topology_state_to_text(conn->topology_state),
			ks_topology_state_to_text(state));

	conn->topology_state = state;
}

void ks_conn_topology_rdlock(struct ks_conn *conn)
{
	pthread_rwlock_rdlock(&conn->topology_lock);
}

void ks_conn_topology_wrlock(struct ks_conn *conn)
{
	pthread_rwlock_wrlock(&conn->topology_lock);
}

void ks_conn_topology_unlock(struct ks_conn *conn)
{
	pthread_rwlock_unlock(&conn->topology_lock);
}


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
#include <libkstreamer/feature.h>
#include <libkstreamer/node.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/pipeline.h>
#include <libkstreamer/xact.h>
#include <libkstreamer/req.h>
#include <libkstreamer/util.h>
#include <libkstreamer/logging.h>
#include <libkstreamer/timer.h>

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

static const char *ks_conn_message_type_to_text(
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

static const char *ks_conn_message_type_to_string(
		enum ks_conn_message_type message_type)
{
	switch((int)message_type) {
	case NLMSG_NOOP:
		return "NOOP";
	case NLMSG_ERROR:
		return "ERROR";
	case NLMSG_DONE:
		return "DONE";
	case NLMSG_OVERRUN:
		return "OVERRUN";
	case KS_NETLINK_BEGIN:
		return "BEGIN";
	case KS_NETLINK_COMMIT:
		return "COMMIT";
	case KS_NETLINK_ABORT:
		return "ABORT";
	case KS_NETLINK_VERSION:
		return "VERSION";
	case KS_NETLINK_FEATURE_GET:
		return "FEATURE_GET";
	case KS_NETLINK_FEATURE_NEW:
		return "FEATURE_NEW";
	case KS_NETLINK_FEATURE_DEL:
		return "FEATURE_DEL";
	case KS_NETLINK_FEATURE_SET:
		return "FEATURE_SET";
	case KS_NETLINK_NODE_GET:
		return "NODE_GET";
	case KS_NETLINK_NODE_NEW:
		return "NODE_NEW";
	case KS_NETLINK_NODE_DEL:
		return "NODE_DEL";
	case KS_NETLINK_NODE_SET:
		return "NODE_SET";
	case KS_NETLINK_CHAN_GET:
		return "CHAN_GET";
	case KS_NETLINK_CHAN_NEW:
		return "CHAN_NEW";
	case KS_NETLINK_CHAN_DEL:
		return "CHAN_DEL";
	case KS_NETLINK_CHAN_SET:
		return "CHAN_SET";
	case KS_NETLINK_PIPELINE_GET:
		return "PIPELINE_GET";
	case KS_NETLINK_PIPELINE_NEW:
		return "PIPELINE_NEW";
	case KS_NETLINK_PIPELINE_DEL:
		return "PIPELINE_DEL";
	case KS_NETLINK_PIPELINE_SET:
		return "PIPELINE_SET";
	}

	return "*INVALID*";
}


struct nlmsghdr *ks_nlmsg_put(
	struct sk_buff *skb, __u32 pid, __u32 seq,
	__u16 message_type, __u16 flags, int payload_size)
{
	int size = NLMSG_LENGTH(payload_size);
	struct nlmsghdr *nlh = skb_put(skb, NLMSG_ALIGN(size));

	if (!nlh)
		return NULL;

	nlh->nlmsg_len = size;
	nlh->nlmsg_type = message_type;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_flags = flags;

	return nlh;
}

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len)
{
	struct ks_attr *attr;

	if (skb_tailroom(skb) < KS_ATTR_SPACE(data_len))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(data_len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(data_len);

	if (data)
		memcpy(KS_ATTR_DATA(attr), data, data_len);

	return 0;
}

static void ks_dump_nlh(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	const char *prefix)
{
	char flags[256];

	snprintf(flags, sizeof(flags),
		"%s%s%s%s%s%s%s",
		nlh->nlmsg_flags & NLM_F_REQUEST ? "NLM_F_REQUEST " : "",
		nlh->nlmsg_flags & NLM_F_MULTI ? "NLM_F_MULTI " : "",
		nlh->nlmsg_flags & NLM_F_ACK ? "NLM_F_ACK " : "",
		nlh->nlmsg_flags & NLM_F_ECHO ? "NLM_F_ECHO " : "",
		nlh->nlmsg_flags & NLM_F_ROOT ? "NLM_F_ROOT " : "",
		nlh->nlmsg_flags & NLM_F_MATCH ? "NLM_F_MATCH " : "",
		nlh->nlmsg_flags & NLM_F_ATOMIC ? "NLM_F_ATOMIC " : "");

	report_conn(conn, LOG_DEBUG,
		"%s  Message type: %s (%d)\n"
		"%s  PID: %d\n"
		"%s  Sequence number: %d\n"
		"%s  Flags: %s\n",
		prefix,
		ks_conn_message_type_to_string(nlh->nlmsg_type),
		nlh->nlmsg_type,
		prefix,
		nlh->nlmsg_pid,
		prefix,
		nlh->nlmsg_seq,
		prefix,
		flags);
#if 0
	__u8 *payload = KS_DATA(nlh);
	int payload_len = KS_PAYLOAD(nlh);
	char *payload_text = alloca(payload_len * 3 + 1);
	int i;

	for(i=0; i<payload_len; i++)
		sprintf(pl + i * 3, "%02x ", payload[i]);

	report_conn(conn, LOG_DEBUG,
		"  Payload: %s\n",
		payload_text);
#endif

}

int ks_conn_sendmsg(struct ks_conn *conn, struct sk_buff *skb)
{
	struct sockaddr_nl dest_sa;
	memset (&dest_sa, 0, sizeof(dest_sa));
	dest_sa.nl_family = AF_NETLINK;
	dest_sa.nl_pid = 0;
	dest_sa.nl_groups = 0;

	struct iovec iov;
	iov.iov_base = skb->data;
	iov.iov_len = skb->len;

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &dest_sa;
	msg.msg_namelen = sizeof(dest_sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	ks_conn_debug_netlink(conn,
		"TX --------- Sending packet len = %d --------\n",
		skb->len);

	struct nlmsghdr *nlh;
	int len_left = skb->len;

	if (conn->debug_netlink) {
		for (nlh = skb->data;
		     NLMSG_OK(nlh, len_left);
		     nlh = NLMSG_NEXT(nlh, len_left))
			ks_dump_nlh(conn, nlh, "TX");
	}

	ks_conn_debug_netlink(conn,
		"TX ------------------------------------------\n");

	int len = sendmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("sendmsg()");
		return -errno;
	}

	kfree_skb(skb);

	return 0;
}

static void ks_conn_request_complete(
	struct ks_conn *conn,
	int err)
{
	conn->cur_req->err = err;

	pthread_mutex_lock(&conn->cur_req->completed_lock);
	conn->cur_req->completed = TRUE;
	pthread_mutex_unlock(&conn->cur_req->completed_lock);
	pthread_cond_broadcast(&conn->cur_req->completed_cond);

	ks_req_put(conn->cur_req);
	conn->cur_req = NULL;

	if (!list_empty(&conn->cur_xact->requests_sent)) {
		ks_timer_start_delta(&conn->timer, 5 * SEC);
		ks_conn_set_state(conn,
			KS_CONN_STATE_WAITING_ACK);
	} else {
		ks_timer_stop(&conn->timer);
		ks_conn_set_state(conn, KS_CONN_STATE_IDLE);
	}
}

static void ks_conn_receive_unicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (conn->cur_xact && nlh->nlmsg_type == KS_NETLINK_BEGIN)
		conn->cur_xact->autocommit = FALSE;

	switch(conn->state) {
	case KS_CONN_STATE_NULL:
		report_conn(conn, LOG_ERR,
			"Unexpected message in state NULL\n");
	break;

	case KS_CONN_STATE_IDLE:
		report_conn(conn, LOG_ERR,
			"Unexpected message in state IDLE\n");
	break;

	case KS_CONN_STATE_WAITING_ACK: {

		assert(conn->cur_xact);
		assert(!conn->cur_req);

		assert(!list_empty(&conn->cur_xact->requests_sent));

		pthread_mutex_lock(&conn->cur_xact->requests_lock);
		conn->cur_req = list_entry(conn->cur_xact->requests_sent.next,
						struct ks_req, node);
		list_del(&conn->cur_req->node);
		pthread_mutex_unlock(&conn->cur_xact->requests_lock);

		if (conn->cur_req->response_callback) {
			conn->cur_req->response_callback(conn->cur_req, nlh);
		} else {
			conn->cur_req->response_payload =
				malloc(KS_PAYLOAD(nlh));

			if (!conn->cur_req->response_payload) {
				//FIXME
				abort();
			}

			memcpy(conn->cur_req->response_payload,
					NLMSG_DATA(nlh),
					NLMSG_PAYLOAD(nlh, 0));
		}

		if (nlh->nlmsg_type == NLMSG_ERROR) {
			ks_conn_request_complete(conn,
				-*((__u32 *)NLMSG_DATA(nlh)));

			break;
		}

		if (nlh->nlmsg_type != conn->cur_req->type) {
			report_conn(conn, LOG_ERR,
				"Message type different: %d %d\n",
				nlh->nlmsg_type, conn->cur_req->type);

			break;
		}

		if (nlh->nlmsg_flags & NLM_F_MULTI) {
			ks_timer_start_delta(&conn->timer, 5 * SEC);
			ks_conn_set_state(conn,
				KS_CONN_STATE_WAITING_DONE);
		} else {
			ks_conn_request_complete(conn, 0);
		}

	}
	break;

	case KS_CONN_STATE_WAITING_DONE:

		assert(conn->cur_xact);
		assert(conn->cur_req);

		if (conn->cur_req->response_callback)
			conn->cur_req->response_callback(conn->cur_req, nlh);

		if (nlh->nlmsg_type == NLMSG_DONE ||
		    nlh->nlmsg_type == NLMSG_ERROR) {

			int err;
			if (nlh->nlmsg_type == NLMSG_ERROR)
				err = -*((__u32 *)NLMSG_DATA(nlh));
			else
				err = 0;

			ks_conn_request_complete(conn, err);
		}
	break;
	}

	if (conn->cur_xact &&
	    (conn->cur_xact->autocommit ||
	    nlh->nlmsg_type == KS_NETLINK_COMMIT)) {

		pthread_mutex_lock(&conn->cur_xact->state_lock);
		conn->cur_xact->state = KS_XACT_STATE_COMPLETED;
		pthread_mutex_unlock(&conn->cur_xact->state_lock);
		pthread_cond_broadcast(&conn->cur_xact->state_cond);
		ks_xact_put(conn->cur_xact);
		conn->cur_xact = NULL;
	}
}

static void ks_conn_receive_multicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (conn->topology_state == KS_TOPOLOGY_STATE_SYNCHED)
		ks_topology_update(conn, nlh);
}

static void ks_conn_receive_msg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	struct sockaddr_nl *src_sa)
{
#if 0
	__u8 *data = NLMSG_DATA(nlh);
	__u8 *text = alloca(NLMSG_PAYLOAD(nlh, 0) * 3 + 1);
	int i;
	for(i=0; i<NLMSG_PAYLOAD(nlh, 0); i++)
		sprintf(text + i * 3, "%02x ", *(data + i));

	report_conn(conn, LOG_DEBUG, "%s\n", text);
#endif

	if (conn->debug_netlink)
		ks_dump_nlh(conn, nlh, "RX");

	if (src_sa->nl_groups)
		ks_conn_receive_multicast(conn, nlh);
	else
		ks_conn_receive_unicast(conn, nlh);

}

static int ks_conn_receive(struct ks_conn *conn)
{
	int buf_size = NLMSG_SPACE(8192);
	void *buf = malloc(buf_size);
	memset(buf, 0, buf_size);

	struct sockaddr_nl src_sa;

	struct iovec iov;
	iov.iov_base = buf;
	iov.iov_len = buf_size;

	struct msghdr msg = {};
	msg.msg_name = &src_sa;
	msg.msg_namelen = sizeof(src_sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	int len = recvmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("recvmsg()");
		free(buf);
		return -errno;
	}

	ks_conn_debug_netlink(conn,
		"RX --------- Received packet len = %d groups = %d"
		"--------\n"
		,len, src_sa.nl_groups);

	struct nlmsghdr *nlh;
	int len_left = len;

	for (nlh = buf;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_conn_receive_msg(conn, nlh, &src_sa);

	free(buf);

	ks_conn_debug_netlink(conn,
		"RX -------------------------------------------\n");

	return 0;
}

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

static void ks_conn_timers_updated(struct ks_timerset *set)
{
	struct ks_conn *conn = container_of(set, struct ks_conn, timerset);
	
	ks_conn_send_message(conn, KS_CONN_MSG_REFRESH, NULL, 0);
}

static void ks_conn_timer(void *data)
{
	struct ks_conn *conn = data;

	ks_conn_debug_netlink(conn, "Timer fired in state %s\n",
		ks_conn_state_to_text(conn->state));

	switch(conn->state) {
	case KS_CONN_STATE_NULL:
	case KS_CONN_STATE_IDLE:
		assert(0);
	break;

	case KS_CONN_STATE_WAITING_ACK:
		report_conn(conn, LOG_ERR, "Timeout waiting for ACK\n");

//		ks_timer_start_delta(&conn->timer, 5 * SEC);
//		ks_conn_set_state(conn,
//			KS_CONN_STATE_WAITING_ACK);
	break;

	case KS_CONN_STATE_WAITING_DONE:
		report_conn(conn, LOG_ERR, "Timeout waiting for DONE\n");

//		ks_conn_request_complete(conn, -ETIMEDOUT);
	break;
	}
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

	ks_timerset_init(&conn->timerset, ks_conn_timers_updated);
	ks_timer_init(&conn->timer, &conn->timerset, "ks_conn",
		ks_conn_timer, conn);

	conn->report_func = ks_report_default;

	return conn;
}

static void ks_xact_flush(struct ks_xact *xact)
{
	if (xact->out_skb) {
		if (xact->out_skb->len) {
			ks_conn_sendmsg(xact->conn, xact->out_skb);
			xact->out_skb = NULL;
		}
	}
}

static int ks_conn_send_requests(
	struct ks_conn *conn,
	struct ks_xact *xact)
{
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

	ks_timer_start_delta(&conn->timer, 5 * SEC);
	ks_conn_set_state(conn, KS_CONN_STATE_WAITING_ACK);

	return 0;
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

	return ks_conn_send_requests(conn, xact);
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

static int ks_conn_receive_message(struct ks_conn *conn)
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

	report_conn(conn, LOG_DEBUG, "Received message %s (len=%d)\n",
		ks_conn_message_type_to_text(msg->type), msg->len);

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
		ks_timerset_run(&conn->timerset);

		longtime_t timeout = ks_timerset_next(&conn->timerset);

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = max(timeout / 1000, 1LL);

		ks_conn_debug_netlink(conn,
			"set poll timeout = %d ms\n", timeout_ms);

		int res;
		res = poll(pollfds, ARRAY_SIZE(pollfds), timeout_ms);
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

			if (ks_conn_receive_message(conn))
				break;

			if (conn->state == KS_CONN_STATE_IDLE)
				ks_conn_send_next_packet(conn);
		}

		if (pollfds[1].revents & POLLHUP ||
		    pollfds[1].revents & POLLERR ||
		    pollfds[1].revents & POLLNVAL ||
		    pollfds[1].revents & POLLIN) {
			if (ks_conn_receive(conn) < 0)
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

void ks_conn_set_state(
	struct ks_conn *conn,
	enum ks_conn_state state)
{
	ks_conn_debug_state(conn, 
		"Conn state changed from %s to %s\n",
		ks_conn_state_to_text(conn->state),
		ks_conn_state_to_text(state));

	conn->state = state;
}

void ks_conn_set_topology_state(
	struct ks_conn *conn,
	enum ks_topology_state state)
{
	ks_conn_debug_state(conn, 
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


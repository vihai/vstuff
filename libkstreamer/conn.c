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
	case KS_TOPOLOGY_STATE_INVALID:
		return "INVALID";
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
	case KS_NETLINK_VERSION:
		return "VERSION";
	case KS_NETLINK_TOPOLOGY_LOCK:
		return "TOPOLOGY_LOCK";
	case KS_NETLINK_TOPOLOGY_TRYLOCK:
		return "TOPOLOGY_TRYLOCK";
	case KS_NETLINK_TOPOLOGY_UNLOCK:
		return "TOPOLOGY_UNLOCK";
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

int ks_conn_sync(struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = NLMSG_NOOP;
	req->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		goto err_req_result;
	}

	ks_req_put(req);

	return 0;

err_req_result:
	ks_req_put(req);
err_req_alloc:
	return err;
}

static int ks_conn_get_version(struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_VERSION;
	req->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		goto err_req_result;
	}

	memcpy(&conn->version, NLMSG_DATA(req->response_payload),
			sizeof(conn->version));

	ks_req_put(req);

	return 0;

err_req_result:
	ks_req_put(req);
err_req_alloc:
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

static void ks_conn_timer(struct ks_timer *timer, enum ks_timer_action action, void *start_data)
{
	struct ks_conn *conn = timer->data;

	switch(action) {
	case KS_TIMER_STOPPED:
		timer->data = NULL;
		ks_conn_debug_netlink(conn, "Conn timer stopped\n");
	break;

	case KS_TIMER_STARTED:
		timer->data = start_data;
	break;

	case KS_TIMER_FIRED:
		ks_conn_debug_netlink(conn, "Conn timer fired\n");
		timer->data = NULL;
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

	conn->debug_netlink = FALSE;
	conn->debug_router = FALSE;
	conn->debug_state = FALSE;

	conn->seqnum = 0xF1CA;
	conn->pid = getpid();

	pthread_mutex_init(&conn->requests_lock, NULL);
	INIT_LIST_HEAD(&conn->requests_pending);
	INIT_LIST_HEAD(&conn->requests_waiting_ack);
	INIT_LIST_HEAD(&conn->requests_multi);

	pthread_mutex_init(&conn->refcnt_lock, NULL);
	pthread_rwlock_init(&conn->topology_lock, NULL);

	ks_timerset_init(&conn->timerset, ks_conn_timers_updated);
	ks_timer_create(&conn->timer, &conn->timerset, "ks_conn",
		ks_conn_timer);

	conn->report_func = ks_report_default;

	conn->state = KS_CONN_STATE_DISCONNECTED;

	return conn;
}

void ks_conn_queue_request(
	struct ks_conn *conn,
	struct ks_req *req)
{
	pthread_mutex_lock(&conn->requests_lock);

	req->id = conn->seqnum << 16;
	conn->seqnum++;

	list_add_tail(&ks_req_get(req)->node, &conn->requests_pending);
	pthread_mutex_unlock(&conn->requests_lock);
}

void ks_conn_flush_requests(struct ks_conn *conn)
{
	ks_conn_send_message(conn, KS_CONN_MSG_REFRESH, NULL, 0);
}

int ks_conn_remote_topology_lock(struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_TOPOLOGY_LOCK;
	req->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		goto err_req_result;
	}

	ks_req_put(req);

	return 0;

err_req_result:
	ks_req_put(req);
err_req_alloc:

	return err;
}

int ks_conn_remote_topology_trylock(struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_TOPOLOGY_LOCK;
	req->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		goto err_req_result;
	}

	ks_req_put(req);

	return 0;

err_req_result:
	ks_req_put(req);
err_req_alloc:
	return err;
}

int ks_conn_remote_topology_unlock(struct ks_conn *conn)
{
	int err;

	struct ks_req *req;
	req = ks_req_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_req_alloc;
	}

	req->type = KS_NETLINK_TOPOLOGY_UNLOCK;
	req->flags = NLM_F_REQUEST;

	ks_conn_queue_request(conn, req);
	ks_conn_flush_requests(conn);

	ks_req_wait(req);
	if (req->err < 0) {
		err = req->err;
		goto err_req_result;
	}

	ks_req_put(req);

	return 0;

err_req_result:
	ks_req_put(req);
err_req_alloc:
	return err;
}

static void ks_conn_nlmsg_dump(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	const char *prefix)
{
	char flags[256];

#if 0
	__u8 *data = NLMSG_DATA(nlh);
	__u8 *text = alloca(NLMSG_PAYLOAD(nlh, 0) * 3 + 1);
	int i;
	for(i=0; i<NLMSG_PAYLOAD(nlh, 0); i++)
		sprintf(text + i * 3, "%02x ", *(data + i));

	report_conn(conn, LOG_DEBUG, "%s\n", text);
#endif


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
		"%s---------- Msg ---------\n", prefix);

	report_conn(conn, LOG_DEBUG,
		"%s  Message type: %s (%d)\n",
		prefix,
		ks_conn_message_type_to_string(nlh->nlmsg_type),
		nlh->nlmsg_type);

	report_conn(conn, LOG_DEBUG,
		"%s  PID: %d\n",
		prefix,
		nlh->nlmsg_pid);

	report_conn(conn, LOG_DEBUG,
		"%s  Sequence number: %08x\n",
		prefix,
		nlh->nlmsg_seq);

	report_conn(conn, LOG_DEBUG,
		"%s  Flags: %s\n",
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


	switch(nlh->nlmsg_type) {
	case NLMSG_NOOP:
	case NLMSG_OVERRUN:
	case NLMSG_ERROR:
	case NLMSG_DONE:
	break;

	case KS_NETLINK_TOPOLOGY_LOCK:
	case KS_NETLINK_TOPOLOGY_TRYLOCK:
	case KS_NETLINK_TOPOLOGY_UNLOCK:
	break;

	case KS_NETLINK_FEATURE_NEW:
	case KS_NETLINK_FEATURE_DEL:
	case KS_NETLINK_FEATURE_SET:
		ks_feature_nlmsg_dump(conn, nlh, prefix);
	break;

	case KS_NETLINK_NODE_NEW:
	case KS_NETLINK_NODE_DEL:
	case KS_NETLINK_NODE_SET:
		ks_node_nlmsg_dump(conn, nlh, prefix);
	break;

	case KS_NETLINK_CHAN_NEW:
	case KS_NETLINK_CHAN_DEL:
	case KS_NETLINK_CHAN_SET:
		ks_chan_nlmsg_dump(conn, nlh, prefix);
	break;

	case KS_NETLINK_PIPELINE_NEW:
	case KS_NETLINK_PIPELINE_DEL:
	case KS_NETLINK_PIPELINE_SET:
		ks_pipeline_nlmsg_dump(conn, nlh, prefix);
	break;
	}
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
			ks_conn_nlmsg_dump(conn, nlh, "TX");
	}

	ks_conn_debug_netlink(conn,
		"TX ------------------------------------------\n");

	int len = sendmsg(conn->sock, &msg, 0);
	if(len < 0) {
		report_conn(conn, LOG_ERR,
			"sendmsg() error: %s\n",
			strerror(errno));
		return -errno;
	}

	kfree_skb(skb);

	return 0;
}

static void ks_conn_receive_acknowledge(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	pthread_mutex_lock(&conn->requests_lock);

	if (list_empty(&conn->requests_waiting_ack)) {
		pthread_mutex_unlock(&conn->requests_lock);
		report_conn(conn, LOG_ERR,
			"Received ACK with no requests waiting for ACK\n");
		return;
	}

	struct ks_req *req;
	struct ks_req *t;
	list_for_each_entry_safe(req, t, &conn->requests_waiting_ack, node) {
		if (req->id == nlh->nlmsg_seq)
			goto found;
	}

	return;

found:
	list_del(&req->node);

	assert(!req->response_payload);
	assert(!req->response_payload_size);

	if (nlh->nlmsg_type == NLMSG_ERROR) {
		pthread_mutex_unlock(&conn->requests_lock);

		if (NLMSG_PAYLOAD(nlh, 0) < sizeof(__u32)) {
			report_conn(conn, LOG_ERR,
				"Error in error message: missing error code.");
		} else {
			ks_req_complete(req,
				-*((__u32 *)NLMSG_DATA(nlh)));
		}

		ks_req_put(req);
		req = NULL;

		return;
	}

	ks_req_resp_append_payload(req, nlh);

	if (nlh->nlmsg_flags & NLM_F_MULTI) {
		list_add_tail(&req->node, &conn->requests_multi);
		ks_timer_start_delta(&req->timer, 5 * SEC, req);
		pthread_mutex_unlock(&conn->requests_lock);

		return;
	}

	pthread_mutex_unlock(&conn->requests_lock);

	ks_req_complete(req, 0);

	ks_req_put(req);
	req = NULL;
}

static void ks_conn_receive_multi(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	struct ks_req *req;
	pthread_mutex_lock(&conn->requests_lock);

	struct ks_req *t;
	list_for_each_entry_safe(req, t, &conn->requests_multi, node) {

		if ((req->id >> 16) == (nlh->nlmsg_seq >> 16)) {

			if ((nlh->nlmsg_seq & 0xffff) != req->multi_seq) {
				report_conn(conn, LOG_ERR,
					"Out of sequence frame, expecting seq"
					" %04x, received %04x\n",
					nlh->nlmsg_seq & 0xffff,
					req->multi_seq);

				ks_req_complete(req, -EIO);
			}

			if (nlh->nlmsg_type == NLMSG_ERROR) {
				pthread_mutex_unlock(&conn->requests_lock);

				if (NLMSG_PAYLOAD(nlh, 0) < sizeof(__u32)) {
					report_conn(conn, LOG_ERR,
						"Error in error message:"
						" missing error code.");
				} else {
					ks_req_complete(req,
						-*((__u32 *)NLMSG_DATA(nlh)));
				}

				ks_req_put(req);
				req = NULL;

				return;
			}

			ks_req_resp_append_payload(req, nlh);

			req->multi_seq++;

			if (nlh->nlmsg_type == NLMSG_DONE) {
				ks_conn_debug_netlink(conn,
					"Multipart response DONE!\n");

				list_del(&req->node);
				ks_req_complete(req, 0);
				ks_req_put(req);
			}

			pthread_mutex_unlock(&conn->requests_lock);
			return;
		}
	}

	pthread_mutex_unlock(&conn->requests_lock);

	report_conn(conn, LOG_ERR,
		"Dropping frame %04x not associated to an active request\n",
		nlh->nlmsg_seq);
}

static void ks_conn_receive_unicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (nlh->nlmsg_flags & NLM_F_ACK) {
		ks_conn_receive_acknowledge(conn, nlh);
		return;
	}

	if (nlh->nlmsg_flags & NLM_F_MULTI) {
		ks_conn_receive_multi(conn, nlh);
		return;
	}

	report_conn(conn, LOG_ERR,
		"Don't know how to handle non-ack, non-multi packet\n");
}

static void ks_conn_receive_multicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (!conn->multicast_seqnum)
		conn->multicast_seqnum = nlh->nlmsg_seq;
	else if (conn->multicast_seqnum != nlh->nlmsg_seq) {
		report_conn(conn, LOG_ERR,
			"Detected lost multicast, topology marked invalid\n");

		pthread_rwlock_wrlock(&conn->topology_lock);
		ks_conn_set_topology_state(conn, KS_TOPOLOGY_STATE_INVALID);
		pthread_rwlock_unlock(&conn->topology_lock);

		conn->multicast_seqnum = 0;
		return;
	}

	if (conn->topology_state == KS_TOPOLOGY_STATE_SYNCHED)
		ks_topology_update(conn, nlh);

	conn->multicast_seqnum++;
}

static void ks_conn_receive_msg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	struct sockaddr_nl *src_sa)
{
	if (conn->debug_netlink)
		ks_conn_nlmsg_dump(conn, nlh, "RX");

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
		report_conn(conn, LOG_ERR,
			"recvmsg() error: %s\n", strerror(errno));
		free(buf);
		return -errno;
	}

	ks_conn_debug_netlink(conn,
		"RX========= Received packet len=%-3d groups=%d =========\n"
		,len, src_sa.nl_groups);

	struct nlmsghdr *nlh;
	int len_left = len;

	for (nlh = buf;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_conn_receive_msg(conn, nlh, &src_sa);

	free(buf);

	ks_conn_debug_netlink(conn,
		"RX====================================================\n");

	return 0;
}



static int ks_conn_send_requests(struct ks_conn *conn)
{
	struct ks_req *req;
	struct sk_buff *skb = NULL;

	pthread_mutex_lock(&conn->requests_lock);
	struct ks_req *t;
	list_for_each_entry_safe(req, t, &conn->requests_pending, node) {
retry:
		if (!skb)
			skb = alloc_skb(4096, GFP_KERNEL);

		if (!skb)
			return -ENOMEM;

		void *oldtail = skb->tail;

		struct nlmsghdr *nlh;
		nlh = ks_nlmsg_put(skb, conn->pid, req->id,
						req->type, req->flags, 0);
		if (!nlh) {
			ks_conn_sendmsg(conn, skb);
			skb = NULL;

			goto retry;
		}

		if (req->skb) {
			if (!skb)
				skb = alloc_skb(4096, GFP_KERNEL);

			if (!skb)
				return -ENOMEM;

			if (req->skb->len >= skb_tailroom(skb)) {
				skb_trim(skb, oldtail - skb->data);

				ks_conn_sendmsg(conn, skb);
				skb = NULL;

				goto retry;
			}

			memcpy(skb_put(skb, req->skb->len),
				req->skb->data, req->skb->len); 
		}

		nlh->nlmsg_len = skb->tail - oldtail;

		list_del(&req->node);
		list_add_tail(&req->node, &conn->requests_waiting_ack);

		ks_timer_start_delta(&req->timer, 5 * SEC, req);
	}
	pthread_mutex_unlock(&conn->requests_lock);

	if (skb && skb->len) {
		ks_conn_sendmsg(conn, skb);
		skb = NULL;
	}

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

		int res = read(conn->cmd_read, &msg->data, msg->len);
		if (res < 0)
			;
	} else
		msg = &msgh;

	ks_conn_debug_netlink(conn,
		"Received message %s (len=%d)\n",
		ks_conn_message_type_to_text(msg->type), msg->len);

	if (msg->type == KS_CONN_MSG_CLOSE)
		return TRUE;
	else if (msg->type == KS_CONN_MSG_REFRESH)
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

		}

		if (pollfds[1].revents & POLLHUP ||
		    pollfds[1].revents & POLLERR ||
		    pollfds[1].revents & POLLNVAL ||
		    pollfds[1].revents & POLLIN) {
			ks_conn_receive(conn);
		}

		ks_conn_send_requests(conn);
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

	conn->cmd_read = filedes[0];
	conn->cmd_write = filedes[1];

	conn->sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KSTREAMER);
	if (conn->sock < 0) {
		report_conn(conn, LOG_ERR,
			"unable to open kstreamer's netlink socket: %s\n",
			strerror(errno));
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
		report_conn(conn, LOG_ERR,
			"unable to bind kstreamer's netlink socket: %s\n",
			strerror(errno));
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

	conn->state = KS_CONN_STATE_ESTABLISHED;

	return 0;

err_invalid_version:
err_get_version:
	ks_conn_send_message(conn, KS_CONN_MSG_CLOSE, NULL, 0);
	pthread_join(conn->protocol_thread, NULL);
err_pthread_create:
	pthread_attr_destroy(&attr);
err_pthread_attr_init:
err_bind:
	close(conn->sock);
err_socket:
err_fcntl_1:
err_fcntl_0:
	close(conn->cmd_read);
	close(conn->cmd_write);
err_pipe:

	return err;
}

void ks_conn_disconnect(struct ks_conn *conn)
{
	assert(conn->state == KS_CONN_STATE_ESTABLISHED);

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
	close(conn->sock);

	ks_pipeline_flush(conn);
	ks_chan_flush(conn);
	ks_node_flush(conn);
	ks_feature_flush(conn);

	conn->state = KS_CONN_STATE_DISCONNECTED;
}

void ks_conn_destroy(struct ks_conn *conn)
{
	if (conn->state == KS_CONN_STATE_ESTABLISHED)
		ks_conn_disconnect(conn);

	assert(conn->state == KS_CONN_STATE_DISCONNECTED);

	pthread_mutex_destroy(&conn->requests_lock);
	pthread_rwlock_destroy(&conn->topology_lock);
	pthread_mutex_destroy(&conn->refcnt_lock);

	free(conn);
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


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
#include <errno.h>
#include <assert.h>

#include <sys/socket.h>

#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include "libkstreamer.h"
#include "netlink.h"
#include "dynattr.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"
#include "xact.h"
#include "req.h"

static const char *ks_netlink_message_type_to_string(
		enum ks_netlink_message_type message_type)
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
	case KS_NETLINK_DYNATTR_GET:
		return "DYNATTR_GET";
	case KS_NETLINK_DYNATTR_NEW:
		return "DYNATTR_NEW";
	case KS_NETLINK_DYNATTR_DEL:
		return "DYNATTR_DEL";
	case KS_NETLINK_DYNATTR_SET:
		return "DYNATTR_SET";
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

static void ks_dump_nlh(struct nlmsghdr *nlh)
{
	fprintf(stderr, "  Message type: %s (%d)\n",
		ks_netlink_message_type_to_string(nlh->nlmsg_type),
		nlh->nlmsg_type);

	fprintf(stderr, "  PID: %d\n",
		nlh->nlmsg_pid);

	fprintf(stderr, "  Sequence number: %d\n",
		nlh->nlmsg_seq);

	fprintf(stderr, "  Flags: ");
	if (nlh->nlmsg_flags & NLM_F_REQUEST)
		fprintf(stderr, "NLM_F_REQUEST ");
	if (nlh->nlmsg_flags & NLM_F_MULTI)
		fprintf(stderr, "NLM_F_MULTI ");
	if (nlh->nlmsg_flags & NLM_F_ACK)
		fprintf(stderr, "NLM_F_ACK ");
	if (nlh->nlmsg_flags & NLM_F_ECHO)
		fprintf(stderr, "NLM_F_ECHO ");
	if (nlh->nlmsg_flags & NLM_F_ROOT)
		fprintf(stderr, "NLM_F_ROOT ");
	if (nlh->nlmsg_flags & NLM_F_MATCH)
		fprintf(stderr, "NLM_F_MATCH ");
	if (nlh->nlmsg_flags & NLM_F_ATOMIC)
		fprintf(stderr, "NLM_F_ATOMIC ");
	fprintf(stderr, "\n");

	/*
	__u8 *payload = KS_DATA(nlh);
	int payload_len = KS_PAYLOAD(nlh);
	int i;

	fprintf(stderr, "  ");
	for(i=0; i<payload_len; i++)
		fprintf(stderr, "%02x ", payload[i]);
	fprintf(stderr, "\n");
	*/
}

static int ks_netlink_sendmsg(struct ks_conn *conn, struct sk_buff *skb)
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

	fprintf(stderr, "\n"
	       ">>>--------- Sending packet len = %d -------->>>\n", skb->len);

	struct nlmsghdr *nlh;
	int len_left = skb->len;

	for (nlh = skb->data;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_dump_nlh(nlh);

	fprintf(stderr, ">>>------------------------------------------<<<\n");

	int len = sendmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("sendmsg()");
		return -errno;
	}

	kfree_skb(skb);

	return 0;
}

static void ks_xact_flush(struct ks_xact *xact)
{
	if (xact->out_skb) {
		if (xact->out_skb->len) {
			ks_netlink_sendmsg(xact->conn, xact->out_skb);
			xact->out_skb = NULL;
		}
	} else {
		kfree_skb(xact->out_skb);
		xact->out_skb = NULL;
	}
}

int ks_send_next_packet(struct ks_conn *conn)
{
	//int size = 0;
	int err;

	if (!conn->cur_xact) {
		if (list_empty(&conn->xacts))
			return 0;

		conn->cur_xact = list_entry(conn->xacts.next, struct ks_xact,
									node);
		list_del(&conn->cur_xact->node);
	}

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

		if (req->request_fill_callback) {
			ks_xact_need_skb(xact);
			if (!xact->out_skb)
				return -ENOMEM;

			err = req->request_fill_callback(req, xact->out_skb,
								req->data);
			if (err == -ENOBUFS) {
				skb_trim(xact->out_skb,
					oldtail - xact->out_skb->data);
				ks_xact_flush(xact);
				goto retry;
			} else if (err < 0)
				return err;
		}

		nlh->nlmsg_len = xact->out_skb->tail - oldtail;

		list_del(&req->node);
		list_add_tail(&req->node, &xact->requests_sent);
	}

	ks_xact_flush(xact);

	ks_conn_set_state(conn, KS_CONN_STATE_WAITING_ACK);

	return 0;
}

static void ks_netlink_request_complete(
	struct ks_conn *conn,
	int err)
{
	conn->cur_req->err = err;
	conn->cur_req->completed = TRUE;

	ks_req_put(conn->cur_req);
	conn->cur_req = NULL;

	if (!list_empty(&conn->cur_xact->requests_sent)) {
		ks_conn_set_state(conn,
			KS_CONN_STATE_WAITING_ACK);
	} else {
		ks_conn_set_state(conn, KS_CONN_STATE_IDLE);
		ks_send_next_packet(conn);
	}
}

static void ks_netlink_receive_unicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (conn->cur_xact && nlh->nlmsg_type == KS_NETLINK_BEGIN)
		conn->cur_xact->autocommit = FALSE;

	switch(conn->state) {
	case KS_CONN_STATE_NULL:
		fprintf(stderr, "Unexpected message in state NULL\n");
	break;

	case KS_CONN_STATE_IDLE:
		fprintf(stderr, "Unexpected message in state IDLE\n");
	break;

	case KS_CONN_STATE_WAITING_ACK: {

		assert(conn->cur_xact);
		assert(!conn->cur_req);

		assert(!list_empty(&conn->cur_xact->requests_sent));

		conn->cur_req = list_entry(conn->cur_xact->requests_sent.next,
						struct ks_req, node);

		list_del(&conn->cur_req->node);

		if (nlh->nlmsg_type == NLMSG_ERROR) {
			ks_netlink_request_complete(conn,
				-*((__u32 *)NLMSG_DATA(nlh)));

			break;
		}

		if (nlh->nlmsg_type != conn->cur_req->type) {
			fprintf(stderr, "Message type different: %d %d\n",
				nlh->nlmsg_type, conn->cur_req->type);

			break;
		}

		if (!conn->cur_req->response_callback) {

			conn->cur_req->response_data =
				malloc(KS_PAYLOAD(nlh));

			if (!conn->cur_req->response_data) {
				//FIXME
				abort();
			}

			memcpy(conn->cur_req->response_data,
					NLMSG_DATA(nlh),
					KS_PAYLOAD(nlh));
		}

		if (conn->cur_req->response_callback)
			conn->cur_req->response_callback(conn->cur_req, nlh,
							conn->cur_req->data);

		if (nlh->nlmsg_flags & NLM_F_MULTI) {
			ks_conn_set_state(conn,
				KS_CONN_STATE_WAITING_DONE);
		} else {
			ks_netlink_request_complete(conn, 0);
		}

	}
	break;

	case KS_CONN_STATE_WAITING_DONE:

		assert(conn->cur_xact);
		assert(conn->cur_req);

		if (conn->cur_req->response_callback)
			conn->cur_req->response_callback(conn->cur_req, nlh,
							conn->cur_req->data);

		if (nlh->nlmsg_type == NLMSG_DONE ||
		    nlh->nlmsg_type == NLMSG_ERROR) {

			list_add_tail(&ks_req_get(conn->cur_req)->node,
					&conn->cur_xact->requests_done);

			int err;
			if (nlh->nlmsg_type == NLMSG_ERROR)
				err = -*((__u32 *)NLMSG_DATA(nlh));
			else
				err = 0;

			ks_netlink_request_complete(conn, err);
		}
	break;
	}

	if (conn->cur_xact &&
	    (conn->cur_xact->autocommit ||
	    nlh->nlmsg_type == KS_NETLINK_COMMIT)) {

		conn->cur_xact->state = KS_XACT_STATE_COMPLETED;
		ks_xact_put(conn->cur_xact);
		conn->cur_xact = NULL;
	}
}

static void ks_netlink_receive_multicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	ks_topology_update(conn, nlh);
}

void ks_netlink_receive_msg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	struct sockaddr_nl *src_sa)
{
#if 0
	__u8 *data = NLMSG_DATA(nlh);
	int i;
	for(i=0; i<len; i++)
		fprintf(stderr, "%02x ", *(data + i));
	fprintf(stderr, "\n");
#endif

	ks_dump_nlh(nlh);

	if (src_sa->nl_groups)
		ks_netlink_receive_multicast(conn, nlh);
	else
		ks_netlink_receive_unicast(conn, nlh);

}

void ks_netlink_receive(struct ks_conn *conn)
{
	int buf_size = NLMSG_SPACE(8192);
	void *buf = malloc(buf_size);
	memset(buf, 0, buf_size);

	struct sockaddr_nl src_sa;

	struct iovec iov;
	iov.iov_base = buf;
	iov.iov_len = buf_size;

	struct msghdr msg;
	msg.msg_name = &src_sa;
	msg.msg_namelen = sizeof(src_sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	int len = recvmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("recvmsg()");
		return;
	}

	fprintf(stderr, "\n"
	       "<<<--------- Received packet len = %d groups = %d--------<<<\n", len, src_sa.nl_groups);

	struct nlmsghdr *nlh;
	int len_left = len;

	for (nlh = buf;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_netlink_receive_msg(conn, nlh, &src_sa);

	fprintf(stderr, "<<<-------------------------------------------<<<\n");
}

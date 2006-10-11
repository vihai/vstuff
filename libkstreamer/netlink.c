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

#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include "netlink.h"
#include "dynattr.h"
#include "node.h"
#include "link.h"
#include "pipeline.h"
#include "request.h"

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
	case KS_NETLINK_DYNATTR_GET:
		return "DYNATTR_NEW";
	case KS_NETLINK_DYNATTR_NEW:
		return "DYNATTR_NEW";
	case KS_NETLINK_DYNATTR_DEL:
		return "DYNATTR_DEL";
	case KS_NETLINK_NODE_GET:
		return "NODE_NEW";
	case KS_NETLINK_NODE_NEW:
		return "NODE_NEW";
	case KS_NETLINK_NODE_DEL:
		return "NODE_DEL";
	case KS_NETLINK_NODE_SET:
		return "NODE_SET";
	case KS_NETLINK_LINK_GET:
		return "LINK_NEW";
	case KS_NETLINK_LINK_NEW:
		return "LINK_NEW";
	case KS_NETLINK_LINK_DEL:
		return "LINK_DEL";
	case KS_NETLINK_LINK_SET:
		return "LINK_SET";
	case KS_NETLINK_PIPELINE_GET:
		return "PIPELINE_NEW";
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

int ks_send_topology_update_req(struct ks_conn *conn)
{
	int err;

	struct sk_buff *skb;
	skb = skb_alloc(NLMSG_SPACE(0) * 6, 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_request *req;
	req = ks_request_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_BEGIN,
			NLM_F_REQUEST, 0);

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_DYNATTR_GET,
			NLM_F_REQUEST | NLM_F_ROOT, 0);

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_NODE_GET,
			NLM_F_REQUEST | NLM_F_ROOT, 0);

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_LINK_GET,
			NLM_F_REQUEST | NLM_F_ROOT, 0);

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_PIPELINE_GET,
			NLM_F_REQUEST | NLM_F_ROOT, 0);

	ks_nlmsg_put(skb, getpid(), req->seqnum, KS_NETLINK_COMMIT,
			NLM_F_REQUEST, 0);

	ks_conn_add_request(conn, req);
	ks_request_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

	ks_request_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
}

int ks_send_noop(struct ks_conn *conn)
{
	int err;

	struct sk_buff *skb;
	skb = skb_alloc(NLMSG_SPACE(0), 0);
	if (!skb) {
		err = -ENOMEM;
		goto err_skb_alloc;
	}

	struct ks_request *req;
	req = ks_request_alloc(conn);
	if (!req) {
		err = -ENOMEM;
		goto err_request_alloc;
	}

	ks_nlmsg_put(skb, getpid(), req->seqnum, NLMSG_NOOP, NLM_F_REQUEST, 0);

	ks_conn_add_request(conn, req);
	ks_request_put(req);

	ks_netlink_sendmsg(conn, skb);

	kfree_skb(skb);

	return 0;

	ks_request_put(req);
err_request_alloc:
	kfree_skb(skb);
err_skb_alloc:

	return err;
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

void netlink_receive_msg(struct ks_conn *conn, struct nlmsghdr *nlh)
{
#if 0
	__u8 *data = NLMSG_DATA(nlh);
	int i;
	for(i=0; i<len; i++)
		printf("%02x ", *(data + i));
	printf("\n");
#endif

	printf("\nMessage type: %s (%d)\n",
		ks_netlink_message_type_to_string(nlh->nlmsg_type),
		nlh->nlmsg_type);

	printf("Sequence number: %d\n",
		nlh->nlmsg_seq);

	printf("Flags: ");
	if (nlh->nlmsg_flags & NLM_F_REQUEST)
		printf("NLM_F_REQUEST ");
	if (nlh->nlmsg_flags & NLM_F_MULTI)
		printf("NLM_F_MULTI ");
	if (nlh->nlmsg_flags & NLM_F_ACK)
		printf("NLM_F_ACK ");
	if (nlh->nlmsg_flags & NLM_F_ECHO)
		printf("NLM_F_ECHO ");
	if (nlh->nlmsg_flags & NLM_F_ROOT)
		printf("NLM_F_ROOT ");
	if (nlh->nlmsg_flags & NLM_F_MATCH)
		printf("NLM_F_MATCH ");
	if (nlh->nlmsg_flags & NLM_F_ATOMIC)
		printf("NLM_F_ATOMIC ");
	printf("\n");

	switch(nlh->nlmsg_type) {
	case NLMSG_NOOP:
	case NLMSG_OVERRUN:
	break;

	case NLMSG_ERROR:
		printf("ERRRRRRRRRRRRROR! %d\n", *((int *)NLMSG_DATA(nlh)));
	break;

	case NLMSG_DONE:
		printf("DONE received\n");
	break;

	case KS_NETLINK_ABORT:
	case KS_NETLINK_COMMIT: {
		struct ks_request *req, *t;
		list_for_each_entry_safe(req, t, &conn->requests, node) {
			if (req->seqnum == nlh->nlmsg_seq) {
				list_del(&req->node);
				ks_request_put(req);
				break;
			}
		}
	}
	break;

	case KS_NETLINK_DYNATTR_NEW: {
		struct ks_dynattr *dynattr;
		dynattr = ks_dynattr_create_from_nlmsg(nlh);
		if (!dynattr) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_add(dynattr);
		ks_dynattr_put(dynattr);
	}
	break;
		
	case KS_NETLINK_DYNATTR_DEL: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(nlh);
		if (!dynattr) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_del(dynattr);
		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_DYNATTR_SET: {
		struct ks_dynattr *dynattr;

		dynattr = ks_dynattr_get_by_nlid(nlh);
		if (!dynattr) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_dynattr_update_from_nlmsg(dynattr, nlh);

		if (conn->dump_packets)
			ks_dynattr_dump(dynattr);

		ks_dynattr_put(dynattr);
	}
	break;

	case KS_NETLINK_NODE_NEW: {
		struct ks_node *node;

		node = ks_node_create_from_nlmsg(nlh);
		if (!node) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_add(node);
		ks_node_put(node);
	}
	break;
		
	case KS_NETLINK_NODE_DEL: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(nlh);
		if (!node) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_del(node);
		ks_node_put(node);
	}
	break;

	case KS_NETLINK_NODE_SET: {
		struct ks_node *node;

		node = ks_node_get_by_nlid(nlh);
		if (!node) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_node_update_from_nlmsg(node, nlh);

		if (conn->dump_packets)
			ks_node_dump(node);

		ks_node_put(node);
	}
	break;

	case KS_NETLINK_LINK_NEW: {
		struct ks_link *link;

		link = ks_link_create_from_nlmsg(nlh);
		if (!link) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_link_dump(link);

		ks_link_add(link); // CHECK FOR DUPEs
		ks_link_put(link);
	}
	break;

	case KS_NETLINK_LINK_DEL: {
		struct ks_link *link;

		link = ks_link_get_by_nlid(nlh);
		if (!link) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_link_dump(link);

		ks_link_del(link);
		ks_link_put(link);
	}
	break;

	case KS_NETLINK_LINK_SET: {
		struct ks_link *link;

		link = ks_link_get_by_nlid(nlh);
		if (!link) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_link_update_from_nlmsg(link, nlh);

		if (conn->dump_packets)
			ks_link_dump(link);

		ks_link_put(link);
	}
	break;

	case KS_NETLINK_PIPELINE_NEW: {
		struct ks_pipeline *pipeline;
		pipeline = ks_pipeline_create_from_nlmsg(nlh);
		if (!pipeline) {
			// FIXME
		}

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_add(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;
		
	case KS_NETLINK_PIPELINE_DEL: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(nlh);
		if (!pipeline) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_del(pipeline);
		ks_pipeline_put(pipeline);
	}
	break;

	case KS_NETLINK_PIPELINE_SET: {
		struct ks_pipeline *pipeline;

		pipeline = ks_pipeline_get_by_nlid(nlh);
		if (!pipeline) {
			fprintf(stderr, "Sync lost\n");
			break;
		}

		ks_pipeline_update_from_nlmsg(pipeline, nlh);

		if (conn->dump_packets)
			ks_pipeline_dump(pipeline);

		ks_pipeline_put(pipeline);
	}
	break;
	}
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

	printf("\nReceived packet len = %d\n", len);

	struct nlmsghdr *nlh;
	int len_left = len;

	for (nlh = buf;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		netlink_receive_msg(conn, nlh);
}

void ks_netlink_waitloop(struct ks_conn *conn)
{
	while(!list_empty(&conn->requests))
		ks_netlink_receive(conn);
}

int ks_netlink_sendmsg(struct ks_conn *conn, struct sk_buff *skb)
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

	int len = sendmsg(conn->sock, &msg, 0);
	if(len < 0) {
		kfree_skb(skb);
		perror("sendmsg()");
		return 1;
	}

	return len;
}

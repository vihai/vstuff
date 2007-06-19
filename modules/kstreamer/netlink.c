/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/netlink.h>

#include <kernel_config.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "netlink.h"
#include "dynattr.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"

struct sock *ksnl;

static struct workqueue_struct *ks_netlink_rcv_wq;

static spinlock_t ks_cur_xact_lock = SPIN_LOCK_UNLOCKED;
static struct ks_xact *cur_xact = NULL;
static struct sk_buff_head ks_backlog;

int ks_kobj_make_path(struct kobject *kobj, void *buf)
{
	struct kobject *cur;
	int length = 0;
	int pos;

	for (cur = kobj; cur; cur = cur->parent)
		length += strlen(kobject_name(cur)) + 1;

	if (!buf)
		return length;

	pos = length;

	for (cur = kobj; cur; cur = cur->parent) {

		int len = strlen(kobject_name(cur));

		pos -= len;

		memcpy(buf + pos, kobject_name(cur), len);
		*((char *)buf + --pos) = '/';
	}

	return length;
}

int ks_netlink_put_attr_path(
	struct sk_buff *skb,
	int type,
	struct kobject *kobj)
{
	int len;
	struct ks_attr *attr;

	len = ks_kobj_make_path(kobj, NULL);

	if (unlikely(skb_tailroom(skb) < KS_ATTR_SPACE(len)))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(len);

	ks_kobj_make_path(kobj, KS_ATTR_DATA(attr));

	return 0;
}

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len)
{
	struct ks_attr *attr;

	if (unlikely(skb_tailroom(skb) < KS_ATTR_SPACE(data_len)))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(data_len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(data_len);

	if (data)
		memcpy(KS_ATTR_DATA(attr), data, data_len);

	return 0;
}

static struct ks_xact *ks_xact_alloc(u32 pid, u32 id)
{
	struct ks_xact *xact;

	xact = kmalloc(sizeof(*xact), GFP_ATOMIC);
	if (!xact)
		return NULL;

	memset(xact, 0, sizeof(*xact));

	atomic_set(&xact->refcnt, 1);

	xact->pid = pid;
	xact->id = id;

	return xact;
}

#if 0
static struct ks_xact *ks_xact_get(struct ks_xact *xact)
{
	BUG_ON(!xact);
	BUG_ON(atomic_read(&xact->refcnt) <= 0);
	BUG_ON(atomic_read(&xact->refcnt) > 10000);

	atomic_inc(&xact->refcnt);

	return xact;
}
#endif

static void ks_xact_put(struct ks_xact *xact)
{
	BUG_ON(!xact);
	BUG_ON(atomic_read(&xact->refcnt) <= 0);
	BUG_ON(atomic_read(&xact->refcnt) > 10000);

	if (atomic_dec_and_test(&xact->refcnt)) {
		if (xact->out_skb)
			kfree_skb(xact->out_skb);

		kfree(xact);
	}
}


void ks_xact_need_skb(struct ks_xact *xact)
{
	if (!xact->out_skb)
		xact->out_skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);

	if (!xact->out_skb) {
		ksnl->sk_err = ENOBUFS;
		ksnl->sk_error_report(ksnl);
	}
}

void ks_xact_flush(struct ks_xact *xact)
{
	if (xact->out_skb) {
		if (xact->out_skb->len) {
			int err;

			err = netlink_unicast(ksnl, xact->out_skb,
							xact->pid, 0);
			if (err < 0) {
				ksnl->sk_err = ENOBUFS;
				ksnl->sk_error_report(ksnl);
			}

			xact->out_skb = NULL;
		} else {
			kfree_skb(xact->out_skb);
			xact->out_skb = NULL;
		}
	}
}

void ks_xact_send_control(
	struct ks_xact *xact,
	enum ks_netlink_message_type message_type, u16 flags)
{
	struct nlmsghdr *nlh;

retry:

	ks_xact_need_skb(xact);
	if (!xact->out_skb)
		return;

	nlh = NLMSG_PUT(xact->out_skb, xact->pid, xact->id, message_type, 0);
	nlh->nlmsg_flags = flags;

	return;

nlmsg_failure:
	ks_xact_flush(xact);
	goto retry;
}

void ks_xact_send_error(struct ks_xact *xact, int error)
{
	struct nlmsghdr *nlh;

retry:

	ks_xact_need_skb(xact);
	if (!xact->out_skb)
		return;

	nlh = NLMSG_PUT(xact->out_skb, xact->pid, xact->id,
			NLMSG_ERROR, sizeof(int));
	nlh->nlmsg_flags = 0;

	*((int *)NLMSG_DATA(nlh)) = -error;

	return;

nlmsg_failure:
	ks_xact_flush(xact);
	goto retry;
}

int ks_cmd_done(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	return 0;
}

int ks_cmd_begin(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	xact->flags |= KS_XACT_FLAGS_PERSISTENT;

	ks_xact_send_control(xact, KS_NETLINK_BEGIN, NLM_F_ACK);

	return 0;
}

int ks_cmd_noop(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_xact_send_control(xact, NLMSG_NOOP, NLM_F_ACK);

	return 0;
}

int ks_cmd_version_request(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *req_nlh)
{
	struct ks_netlink_version_response *vr;
	struct nlmsghdr *nlh;

retry:

	ks_xact_need_skb(xact);
	if (!xact->out_skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(xact->out_skb, xact->pid, xact->id,
			KS_NETLINK_VERSION,
			sizeof(*vr));
	nlh->nlmsg_flags = 0;

	vr = (struct ks_netlink_version_response *)NLMSG_DATA(nlh);
	vr->reserved = 0;
	vr->major = 0;
	vr->minor = 0;
	vr->service = 1;

	return 0;

nlmsg_failure:
	ks_xact_flush(xact);
	goto retry;
}

int ks_cmd_commit(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_xact_send_control(xact, KS_NETLINK_COMMIT, NLM_F_ACK);

	xact->flags |= KS_XACT_FLAGS_COMPLETED;

	return 0;
}

int ks_cmd_abort(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_xact_send_control(xact, KS_NETLINK_ABORT, NLM_F_ACK);

	xact->flags |= KS_XACT_FLAGS_COMPLETED;

	return 0;
}

int ks_dynattr_cmd_get(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_xact_send_control(xact, KS_NETLINK_DYNATTR_GET,
			NLM_F_ACK | NLM_F_MULTI);

	ks_dynattr_netlink_dump(xact);

	return 0;
}

int ks_node_cmd_get(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_xact_send_control(xact, KS_NETLINK_NODE_GET,
			NLM_F_ACK | NLM_F_MULTI);

	ks_node_netlink_dump(xact);

	return 0;
}

int ks_cmd_not_implemented(
	struct ks_command *cmd,
	struct ks_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_msg(KERN_INFO, "command %d not implemented\n",
		cmd->message_type);

	return 0;
};

struct ks_command ks_commands[] =
{
	{ NLMSG_DONE, ks_cmd_done, 0 },
	{ NLMSG_NOOP, ks_cmd_noop, 0 },

	{ KS_NETLINK_VERSION, ks_cmd_version_request, 0 },
	{ KS_NETLINK_BEGIN, ks_cmd_begin, 0 },
	{ KS_NETLINK_COMMIT, ks_cmd_commit, 0 },
	{ KS_NETLINK_ABORT, ks_cmd_abort, 0 },

	{ KS_NETLINK_DYNATTR_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_DYNATTR_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_DYNATTR_GET, ks_dynattr_cmd_get, 0 },
	{ KS_NETLINK_DYNATTR_SET, ks_cmd_not_implemented, KS_CMD_WR },

	{ KS_NETLINK_NODE_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_NODE_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_NODE_GET, ks_node_cmd_get, 0 },
	{ KS_NETLINK_NODE_SET, ks_cmd_not_implemented, KS_CMD_WR },

	{ KS_NETLINK_CHAN_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_CHAN_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_CHAN_GET, ks_chan_cmd_get, 0 },
	{ KS_NETLINK_CHAN_SET, ks_chan_cmd_set, KS_CMD_WR },

	{ KS_NETLINK_PIPELINE_NEW, ks_pipeline_cmd_new, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_DEL, ks_pipeline_cmd_del, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_GET, ks_pipeline_cmd_get, 0 },
	{ KS_NETLINK_PIPELINE_SET, ks_pipeline_cmd_set, KS_CMD_WR },
};

static int ks_netlink_rcv_msg(
	struct sk_buff *skb,
	struct nlmsghdr *nlh)
{
	struct ks_command *cmd = NULL;
	int i;
	int err;

	/* Only requests are handled */
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST))
		return -EINVAL;

	/* Do this with a hash TODO */
	for(i=0; i<ARRAY_SIZE(ks_commands); i++) {
		if (ks_commands[i].message_type == nlh->nlmsg_type) {
			cmd = &ks_commands[i];
			break;
		}
	}

	if (!cmd)
		return -EINVAL;

	if (!cur_xact) {
		cur_xact = ks_xact_alloc(nlh->nlmsg_pid,
					nlh->nlmsg_seq);
		if (!cur_xact)
			return -ENOMEM;
	}

	err = cmd->handler(cmd, cur_xact, nlh);
	if (err < 0)
		ks_xact_send_error(cur_xact, err);

	ks_xact_flush(cur_xact);

	if (!(cur_xact->flags & KS_XACT_FLAGS_PERSISTENT) ||
	     cur_xact->flags & KS_XACT_FLAGS_COMPLETED) {
		spin_lock(&ks_cur_xact_lock);
		ks_xact_put(cur_xact);
		cur_xact = NULL;
		spin_unlock(&ks_cur_xact_lock);
	}

	return 0;
}

static int ks_netlink_rcv_skb(struct sk_buff *skb, int *queued)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = (struct nlmsghdr *)skb->data;
	if (cur_xact && cur_xact->pid != nlh->nlmsg_pid) {
		skb_queue_tail(&ks_backlog, skb);
		*queued = TRUE;
		return 0;
	}

	while (skb->len >= NLMSG_SPACE(0)) {
		u32 rlen;

		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) ||
		    skb->len < nlh->nlmsg_len) {
			*queued = FALSE;
			return 0;
		}

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		err = ks_netlink_rcv_msg(skb, nlh);
		if (err < 0)
			netlink_ack(skb, nlh, err);

		skb_pull(skb, rlen);
	}

	return 0;
}

static void ks_run_backlog(void)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ks_backlog))) {

		int queued = FALSE;

		ks_netlink_rcv_skb(skb, &queued);

		if (!queued)
			kfree_skb(skb);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void ks_netlink_rcv_work_func(void *data)
#else
static void ks_netlink_rcv_work_func(struct work_struct *work)
#endif
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ksnl->sk_receive_queue))) {

		int queued = FALSE;

		ks_netlink_rcv_skb(skb, &queued);

		if (!queued)
			kfree_skb(skb);
	}

	if (!cur_xact)
		ks_run_backlog();
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static DECLARE_WORK(ks_netlink_rcv_work, ks_netlink_rcv_work_func, NULL);
#else
static DECLARE_WORK(ks_netlink_rcv_work, ks_netlink_rcv_work_func);
#endif

static void ks_netlink_rcv(struct sock *sk, int len)
{
	/* Packets can only be coming in from ksnl */

	queue_work(ks_netlink_rcv_wq, &ks_netlink_rcv_work);
}

int ks_netlink_modinit(void)
{
	int err;

	skb_queue_head_init(&ks_backlog);

	ks_netlink_rcv_wq = create_workqueue("ksnl");
	if (!ks_netlink_rcv_wq) {
		err = -ENOMEM;
		goto err_create_workqueue;
	}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, ks_netlink_rcv);
#else
	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, 0,
					ks_netlink_rcv, THIS_MODULE);
#endif
	if (!ksnl) {
		err = -ENOMEM;
		goto err_netlink_kernel_create;
	}

	netlink_set_nonroot(NETLINK_KSTREAMER, NL_NONROOT_RECV);

	return 0;

	destroy_workqueue(ks_netlink_rcv_wq);
err_create_workqueue:
	sock_release(ksnl->sk_socket);
err_netlink_kernel_create:

	return err;
}

void ks_netlink_modexit(void)
{
	destroy_workqueue(ks_netlink_rcv_wq);

	sock_release(ksnl->sk_socket);
}


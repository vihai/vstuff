/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2006 Daniele Orlandi
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
#include "link.h"
#include "pipeline.h"

struct sock *ksnl;
static struct workqueue_struct *ks_netlink_rcv_wq;

#define KS_NETLINK_XACTS_HASHBITS	8
#define KS_NETLINK_XACTS_HASHSIZE	((1 << KS_NETLINK_XACTS_HASHBITS) - 1)

static struct hlist_head ks_netlink_xacts_hash[KS_NETLINK_XACTS_HASHSIZE];
spinlock_t ks_netlink_xacts_hash_lock = SPIN_LOCK_UNLOCKED;

static inline struct hlist_head *ks_netlink_xacts_get_hash(u32 pid)
{
	return &ks_netlink_xacts_hash[pid & (KS_NETLINK_XACTS_HASHSIZE - 1)];
}

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

static struct ks_netlink_xact *ks_netlink_xact_alloc(u32 pid, u32 id)
{
	struct ks_netlink_xact *xact;

	xact = kmalloc(sizeof(*xact), GFP_ATOMIC);
	if (!xact)
		return NULL;

	memset(xact, 0, sizeof(*xact));

	atomic_set(&xact->refcnt, 1);

	xact->pid = pid;
	xact->id = id;

	return xact;
}

static struct ks_netlink_xact *ks_netlink_xact_get(struct ks_netlink_xact *xact)
{
	BUG_ON(!xact);
	BUG_ON(atomic_read(&xact->refcnt) <= 0);
	BUG_ON(atomic_read(&xact->refcnt) > 10000);

	atomic_inc(&xact->refcnt);

	return xact;
}

static void ks_netlink_xact_put(struct ks_netlink_xact *xact)
{
	BUG_ON(!xact);
	BUG_ON(atomic_read(&xact->refcnt) <= 0);
	BUG_ON(atomic_read(&xact->refcnt) > 10000);

	if (atomic_dec_and_test(&xact->refcnt)) {
		if (xact->dump_skb)
			kfree_skb(xact->dump_skb);

		kfree(xact);
	}
}


static struct ks_netlink_xact *ks_netlink_xact_get_by_pid(u32 pid)
{
	struct ks_netlink_xact *xact;
	struct hlist_node *t;

	spin_lock(&ks_netlink_xacts_hash_lock);
	hlist_for_each_entry(xact, t, ks_netlink_xacts_get_hash(pid), node) {
		if (xact->pid == pid) {
			ks_netlink_xact_get(xact);
			spin_unlock(&ks_netlink_xacts_hash_lock);
			return xact;
		}
	}
	spin_unlock(&ks_netlink_xacts_hash_lock);

	return NULL;
}

void ks_netlink_xact_need_skb(struct ks_netlink_xact *xact)
{
	if (!xact->dump_skb)
		xact->dump_skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);

	if (!xact->dump_skb) {
		ksnl->sk_err = ENOBUFS;
		ksnl->sk_error_report(ksnl);
	}
}

void ks_netlink_xact_flush(struct ks_netlink_xact *xact)
{

	if (xact->dump_skb) {
		if (xact->dump_skb->len) {
			int err;

			err = netlink_unicast(ksnl, xact->dump_skb,
							xact->pid, 0);
			if (err < 0) {
				ksnl->sk_err = ENOBUFS;
				ksnl->sk_error_report(ksnl);
			}

			xact->dump_skb = NULL;
		} else {
			kfree_skb(xact->dump_skb);
			xact->dump_skb = NULL;
		}
	}
}

void ks_netlink_xact_send_control(struct ks_netlink_xact *xact,
		enum ks_netlink_message_type message_type, u16 flags)
{
	struct nlmsghdr *nlh;

retry:

	ks_netlink_xact_need_skb(xact);
	if (!xact->dump_skb)
		return;

	nlh = NLMSG_PUT(xact->dump_skb, xact->pid, xact->id, message_type, 0);
	nlh->nlmsg_flags = flags;

	return;

nlmsg_failure:
	ks_netlink_xact_flush(xact);
	goto retry;
}

static void ks_netlink_xact_add(struct ks_netlink_xact *xact)
{
	spin_lock(&ks_netlink_xacts_hash_lock);
	hlist_add_head(&ks_netlink_xact_get(xact)->node,
			ks_netlink_xacts_get_hash(xact->pid));
	spin_unlock(&ks_netlink_xacts_hash_lock);
}

int ks_cmd_done(
	struct ks_command *cmd,
       	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	return 0;
}

int ks_cmd_begin(
	struct ks_command *cmd,
       	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	xact->flags |= KS_XACT_FLAGS_PERSISTENT;

	ks_netlink_xact_add(xact);

	return 0;
}

int ks_cmd_commit(
	struct ks_command *cmd,
       	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_netlink_xact_send_control(xact, KS_NETLINK_COMMIT, 0);

	spin_lock(&ks_netlink_xacts_hash_lock);
	hlist_del(&xact->node);
	spin_unlock(&ks_netlink_xacts_hash_lock);

//	up_read(&kstreamer_subsys.rwsem);

	ks_netlink_xact_put(xact);

	return 0;
}

int ks_cmd_abort(
	struct ks_command *cmd,
       	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	ks_netlink_xact_send_control(xact, KS_NETLINK_ABORT, 0);

	spin_lock(&ks_netlink_xacts_hash_lock);
	hlist_del(&xact->node);
	spin_unlock(&ks_netlink_xacts_hash_lock);

//	up_read(&kstreamer_subsys.rwsem);

	ks_netlink_xact_put(xact);

	return 0;
}

int ks_dynattr_cmd_get(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	printk(KERN_DEBUG "==========> Dynattr GET\n");

	ks_dynattr_netlink_dump(xact);

	return 0;
}

int ks_node_cmd_get(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	printk(KERN_DEBUG "==========> Node GET\n");

	ks_node_netlink_dump(xact);

	return 0;
}

int ks_link_cmd_get(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	printk(KERN_DEBUG "==========> Link GET\n");

	ks_link_netlink_dump(xact);

	return 0;
}

int ks_link_cmd_set(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_link *link;

	printk(KERN_DEBUG "==========> Link SET\n");

	link = ks_link_get_by_nlid(nlh);
	if (!link) {
		printk(KERN_CRIT "Link ID not found\n");
		// FIXME
		return -ENODEV;
	}

	ks_link_update_from_nlmsg(link, nlh);

	return 0;
}

int ks_pipeline_cmd_new(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	struct ks_pipeline *pipeline;
	int err;

	printk(KERN_DEBUG "==========> Pipeline NEW\n");

	pipeline = ks_pipeline_create_from_nlmsg(nlh, &err);
	if (!pipeline) {
		// FIXME
		return err;
	}

	ks_pipeline_dump(pipeline);

	err = ks_pipeline_register(pipeline);
	if (err < 0)
		goto err_pipeline_register;

	ks_pipeline_put(pipeline);

	return 0;

	ks_pipeline_unregister(pipeline);
err_pipeline_register:;

	return err;
}

int ks_pipeline_cmd_get(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	printk(KERN_DEBUG "==========> Pipeline GET\n");

	ks_pipeline_netlink_dump(xact);

	return 0;
}

int ks_cmd_not_implemented(
	struct ks_command *cmd,
	struct ks_netlink_xact *xact,
	struct nlmsghdr *nlh)
{
	printk(KERN_DEBUG "kstreamer command %d\n", cmd->message_type);

	return 0;
};

struct ks_command ks_commands[] =
{
	{ NLMSG_DONE, ks_cmd_done, 0 },

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

	{ KS_NETLINK_LINK_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_LINK_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_LINK_GET, ks_link_cmd_get, 0 },
	{ KS_NETLINK_LINK_SET, ks_link_cmd_set, KS_CMD_WR },

	{ KS_NETLINK_PIPELINE_NEW, ks_pipeline_cmd_new, KS_CMD_WR },
//	{ KS_NETLINK_PIPELINE_DEL, ks_pipeline_cmd_del, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_GET, ks_pipeline_cmd_get, 0 },
//	{ KS_NETLINK_PIPELINE_SET, ks_pipeline_cmd_set, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_SET, ks_cmd_not_implemented, KS_CMD_WR },
};

static int ks_netlink_rcv_msg(
	struct sk_buff *skb,
	struct nlmsghdr *nlh)
{
	struct ks_netlink_xact *xact;
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

	xact = ks_netlink_xact_get_by_pid(nlh->nlmsg_pid);
	if (!xact) {
		xact = ks_netlink_xact_alloc(nlh->nlmsg_pid, nlh->nlmsg_seq);
		if (!xact)
			return -ENOMEM;

/*		if (cmd->flags & KS_CMD_WR)
			xact->flags |= KS_XACT_FLAGS_WRITE;

		if (nlh->nlmsg_type == KS_BEGIN_READ ||
		    nlh->nlmsg_type == KS_BEGIN_WRITE) {
			xact->flags |= KS_XACT_FLAGS_PERSISTENT;

		if (xact->flags & KS_XACT_FLAGS_WRITE)
			down_write(&kstreamer_subsys.rwsem);
		else
			down_read(&kstreamer_subsys.rwsem);*/
	}

/*	if (!(xact->flags & KS_XACT_FLAGS_PERSISTENT))
		if (xact->flags & KS_XACT_FLAGS_WRITE)
			down_write(&kstreamer_subsys.rwsem);
		else
			down_read(&kstreamer_subsys.rwsem);
	}*/

	err = cmd->handler(cmd, xact, nlh);

	ks_netlink_xact_flush(xact);
	ks_netlink_xact_put(xact);

/*	if (!(xact->flags & KS_XACT_FLAGS_PERSISTENT))
		if (xact->mode == KS_MODE_WRITE)
			up_write(&kstreamer_subsys.rwsem);
		else
			up_read(&kstreamer_subsys.rwsem);
	}*/

	return err;
}

static int ks_netlink_rcv_skb(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	int err;

	while (skb->len >= NLMSG_SPACE(0)) {
		u32 rlen;

		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) ||
		    skb->len < nlh->nlmsg_len)
			return 0;

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		err = ks_netlink_rcv_msg(skb, nlh);
		if (err < 0)
			netlink_ack(skb, nlh, err);
		else if (nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);

		skb_pull(skb, rlen);
	}

	return 0;
}

static void ks_netlink_rcv_work_func(void *data)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ksnl->sk_receive_queue))) {
		ks_netlink_rcv_skb(skb);
		kfree_skb(skb);
	}
}

static DECLARE_WORK(ks_netlink_rcv_work, ks_netlink_rcv_work_func, NULL);

static void ks_netlink_rcv(struct sock *sk, int len)
{
	/* Packets can only be coming in from ksnl */

	queue_work(ks_netlink_rcv_wq, &ks_netlink_rcv_work);
}

int ks_netlink_modinit(void)
{
	int err;

	int i;
	for (i=0; i<ARRAY_SIZE(ks_netlink_xacts_hash); i++)
		INIT_HLIST_HEAD(&ks_netlink_xacts_hash[i]);

	ks_netlink_rcv_wq = create_workqueue("ksnl");
	if (!ks_netlink_rcv_wq) {
		err = -ENOMEM;
		goto err_create_workqueue;
	}


	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, 0,
					ks_netlink_rcv, THIS_MODULE);
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


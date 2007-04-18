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
#include <linux/module.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "dynattr.h"
#include "netlink.h"

#define KS_CAPA_HASHBITS	8
#define KS_CAPA_HASHSIZE	((1 << KS_CAPA_HASHBITS) - 1)

static struct hlist_head ks_dynattrs_hash[KS_CAPA_HASHSIZE];

struct ks_dynattr *ks_dynattr_get(struct ks_dynattr *dynattr)
{
	atomic_inc(&dynattr->refcnt);

	return dynattr;
}
EXPORT_SYMBOL(ks_dynattr_get);

void ks_dynattr_put(struct ks_dynattr *dynattr)
{
	if (!atomic_dec_and_test(&dynattr->refcnt)) {
		down_write(&kstreamer_subsys.rwsem);
		hlist_del(&dynattr->node);
		up_write(&kstreamer_subsys.rwsem);

		kfree(dynattr);
	}
}
EXPORT_SYMBOL(ks_dynattr_put);

static inline struct hlist_head *ks_dynattrs_get_hash(u32 pid)
{
	return &ks_dynattrs_hash[pid & (KS_CAPA_HASHSIZE - 1)];
}

struct ks_dynattr *_ks_dynattr_search_by_id(int id)
{
	struct ks_dynattr *dynattr;
	int i;

	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(dynattr, t, &ks_dynattrs_hash[i], node)
			if (dynattr->id == id)
				return dynattr;
	}

	return NULL;
}

struct ks_dynattr *ks_dynattr_get_by_id(int id)
{
	struct ks_dynattr *dynattr;

	down_read(&kstreamer_subsys.rwsem);
	dynattr = ks_dynattr_get(_ks_dynattr_search_by_id(id));
	up_read(&kstreamer_subsys.rwsem);

	return dynattr;
}

static int _ks_dynattr_new_id(void)
{
	static int cur_id;

	for (;;) {
		cur_id++;

		if (cur_id < 0x00ff || cur_id > 0xffff)
			cur_id = 0x00ff;

		if (!_ks_dynattr_search_by_id(cur_id))
			return cur_id;
	}
}

static int ks_dynattr_netlink_fill_msg(
	struct ks_dynattr *dynattr,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	u32 pid, u32 seq, u16 flags)
{
	struct nlmsghdr *nlh;
	unsigned char *oldtail;
	int err = -ENOBUFS;

	oldtail = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, message_type, 0);
	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_DYNATTR_ID, &dynattr->id,
						sizeof(dynattr->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_DYNATTR_DEL) {
		err = ks_netlink_put_attr(skb, KS_DYNATTR_NAME,
					dynattr->name, strlen(dynattr->name));
		if (err < 0)
			goto err_put_attr;
	}

	nlh->nlmsg_len = skb->tail - oldtail;

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

static int ks_dynattr_netlink_notification(
	struct ks_dynattr *dynattr,
	enum ks_netlink_message_type message_type,
	u32 pid)
{
	struct sk_buff *skb;
	int err = -EINVAL;

	skb = alloc_skb(NLMSG_SPACE(128), GFP_KERNEL);
	if (!skb) {
	        netlink_set_err(ksnl, 0, KS_NETLINK_GROUP_TOPOLOGY, ENOBUFS);
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	NETLINK_CB(skb).pid = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	NETLINK_CB(skb).dst_pid = pid;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	NETLINK_CB(skb).dst_groups = pid ? 0 : (1 << KS_NETLINK_GROUP_TOPOLOGY);
#else
	NETLINK_CB(skb).dst_group = pid ? 0 : KS_NETLINK_GROUP_TOPOLOGY;
#endif

	err = ks_dynattr_netlink_fill_msg(
			dynattr, skb, message_type, pid, 0, 0);
	if (err < 0)
		goto err_fill_msg;

	netlink_broadcast(ksnl, skb, 0,
		KS_NETLINK_GROUP_TOPOLOGY,
		GFP_KERNEL);

	return 0;

err_fill_msg:
	kfree_skb(skb);
err_alloc_skb:

	return err;
}

void ks_dynattr_netlink_dump(struct ks_xact *xact)
{
	struct ks_dynattr *dynattr;
	int err;
	int i;

	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(dynattr, t, &ks_dynattrs_hash[i], node) {

retry:
			ks_xact_need_skb(xact);
			if (!xact->out_skb)
				return;

			err = ks_dynattr_netlink_fill_msg(dynattr,
						xact->out_skb,
						KS_NETLINK_DYNATTR_NEW,
						xact->pid,
						0,
						NLM_F_MULTI);
			if (err < 0) {
				ks_xact_flush(xact);
				goto retry;
			}
		}
	}

	ks_xact_send_control(xact, NLMSG_DONE, NLM_F_MULTI);
}

struct ks_dynattr *ks_dynattr_register(const char *name)
{
	struct ks_dynattr *dynattr;
	int i;

	down_read(&kstreamer_subsys.rwsem);
	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(dynattr, t, &ks_dynattrs_hash[i], node) {
			if (!strcmp(dynattr->name, name)) {
				up_read(&kstreamer_subsys.rwsem);
				return ks_dynattr_get(dynattr);
			}
		}
	}
	up_read(&kstreamer_subsys.rwsem);

	dynattr = kmalloc(sizeof(*dynattr), GFP_KERNEL);
	if (!dynattr)
		return NULL;

	memset(dynattr, 0, sizeof(*dynattr));

	atomic_set(&dynattr->refcnt, 1);

	strncpy(dynattr->name, name, sizeof(dynattr->name));

	down_write(&kstreamer_subsys.rwsem);
	dynattr->id = _ks_dynattr_new_id();
	hlist_add_head(&dynattr->node, ks_dynattrs_get_hash(dynattr->id));
	up_write(&kstreamer_subsys.rwsem);

	ks_dynattr_netlink_notification(dynattr, KS_NETLINK_DYNATTR_NEW, 0);

	return dynattr;
}
EXPORT_SYMBOL(ks_dynattr_register);

void ks_dynattr_unregister(struct ks_dynattr *dynattr)
{
	ks_dynattr_netlink_notification(dynattr, KS_NETLINK_DYNATTR_DEL, 0);

	ks_dynattr_put(dynattr);
}
EXPORT_SYMBOL(ks_dynattr_unregister);

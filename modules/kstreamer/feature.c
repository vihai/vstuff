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
#include "feature.h"
#include "netlink.h"

#define KS_CAPA_HASHBITS	8
#define KS_CAPA_HASHSIZE	((1 << KS_CAPA_HASHBITS) - 1)

static struct hlist_head ks_features_hash[KS_CAPA_HASHSIZE];

struct ks_feature *ks_feature_get(struct ks_feature *feature)
{
	atomic_inc(&feature->refcnt);

	return feature;
}
EXPORT_SYMBOL(ks_feature_get);

void ks_feature_put(struct ks_feature *feature)
{
	if (!atomic_dec_and_test(&feature->refcnt)) {
		down_write(&kstreamer_subsys_rwsem);
		hlist_del(&feature->node);
		up_write(&kstreamer_subsys_rwsem);

		kfree(feature);
	}
}
EXPORT_SYMBOL(ks_feature_put);

static inline struct hlist_head *ks_features_get_hash(u32 pid)
{
	return &ks_features_hash[pid & (KS_CAPA_HASHSIZE - 1)];
}

struct ks_feature *_ks_feature_search_by_id(int id)
{
	struct ks_feature *feature;
	int i;

	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(feature, t, &ks_features_hash[i], node)
			if (feature->id == id)
				return feature;
	}

	return NULL;
}

struct ks_feature *ks_feature_get_by_id(int id)
{
	struct ks_feature *feature;

	down_read(&kstreamer_subsys_rwsem);
	feature = ks_feature_get(_ks_feature_search_by_id(id));
	up_read(&kstreamer_subsys_rwsem);

	return feature;
}

static int _ks_feature_new_id(void)
{
	static int cur_id;

	for (;;) {
		cur_id++;

		if (cur_id < 0x00ff || cur_id > 0xffff)
			cur_id = 0x00ff;

		if (!_ks_feature_search_by_id(cur_id))
			return cur_id;
	}
}

static int ks_feature_netlink_fill_msg(
	struct ks_feature *feature,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	u32 pid, u32 seq, u16 flags)
{
	struct nlmsghdr *nlh;
	unsigned char *oldtail;
	int err = -ENOBUFS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	oldtail = skb->tail;
#else
	oldtail = skb_tail_pointer(skb);
#endif

	nlh = NLMSG_PUT(skb, pid, seq, message_type, 0);
	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_FEATURE_ID, &feature->id,
						sizeof(feature->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_FEATURE_DEL) {
		err = ks_netlink_put_attr(skb, KS_FEATURE_NAME,
					feature->name, strlen(feature->name));
		if (err < 0)
			goto err_put_attr;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	nlh->nlmsg_len = skb->tail - oldtail;
#else
	nlh->nlmsg_len = skb_tail_pointer(skb) - oldtail;
#endif

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

static int ks_feature_netlink_notification(
	struct ks_feature *feature,
	enum ks_netlink_message_type message_type,
	u32 pid)
{
	struct sk_buff *skb;
	int err = -EINVAL;

	skb = alloc_skb(NLMSG_SPACE(128), GFP_KERNEL);
	if (!skb) {
	        //netlink_set_err(ksnl, 0, KS_NETLINK_GROUP_TOPOLOGY, ENOBUFS);
	        // FIXME, set_err is really needed, but kernel people removed
	        // the EXPORT_SYMBOL
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

	err = ks_feature_netlink_fill_msg(
			feature, skb, message_type, pid, 0, 0);
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

void ks_feature_netlink_dump(struct ks_xact *xact)
{
	struct ks_feature *feature;
	int err;
	int i;

	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(feature, t, &ks_features_hash[i], node) {

retry:
			ks_xact_need_skb(xact);
			if (!xact->out_skb)
				return;

			err = ks_feature_netlink_fill_msg(feature,
						xact->out_skb,
						KS_NETLINK_FEATURE_NEW,
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

struct ks_feature *ks_feature_register(const char *name)
{
	struct ks_feature *feature;
	int i;

	down_read(&kstreamer_subsys_rwsem);
	for(i=0; i<KS_CAPA_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(feature, t, &ks_features_hash[i], node) {
			if (!strcmp(feature->name, name)) {
				up_read(&kstreamer_subsys_rwsem);
				return ks_feature_get(feature);
			}
		}
	}
	up_read(&kstreamer_subsys_rwsem);

	feature = kmalloc(sizeof(*feature), GFP_KERNEL);
	if (!feature)
		return NULL;

	memset(feature, 0, sizeof(*feature));

	atomic_set(&feature->refcnt, 1);

	strncpy(feature->name, name, sizeof(feature->name));

	down_write(&kstreamer_subsys_rwsem);
	feature->id = _ks_feature_new_id();
	hlist_add_head(&feature->node, ks_features_get_hash(feature->id));
	up_write(&kstreamer_subsys_rwsem);

	ks_feature_netlink_notification(feature, KS_NETLINK_FEATURE_NEW, 0);

	return feature;
}
EXPORT_SYMBOL(ks_feature_register);

void ks_feature_unregister(struct ks_feature *feature)
{
	ks_feature_netlink_notification(feature, KS_NETLINK_FEATURE_DEL, 0);

	ks_feature_put(feature);
}
EXPORT_SYMBOL(ks_feature_unregister);

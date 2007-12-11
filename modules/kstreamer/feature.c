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

#define KS_FEATURE_HASHBITS	8
#define KS_FEATURE_HASHSIZE	((1 << KS_FEATURE_HASHBITS) - 1)

static struct hlist_head ks_features_hash[KS_FEATURE_HASHSIZE];
static rwlock_t ks_features_list_lock = RW_LOCK_UNLOCKED;

struct ks_feature *ks_feature_get(struct ks_feature *feature)
{
	atomic_inc(&feature->refcnt);

	return feature;
}
EXPORT_SYMBOL(ks_feature_get);

void ks_feature_put(struct ks_feature *feature)
{
	if (!atomic_dec_and_test(&feature->refcnt))
		kfree(feature);
}
EXPORT_SYMBOL(ks_feature_put);

static inline struct hlist_head *ks_features_get_hash(u32 pid)
{
	return &ks_features_hash[pid & (KS_FEATURE_HASHSIZE - 1)];
}

struct ks_feature *_ks_feature_search_by_id(int id)
{
	struct ks_feature *feature;
	int i;

	for(i=0; i<KS_FEATURE_HASHSIZE; i++) {
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

	read_lock(&ks_features_list_lock);
	feature = ks_feature_get(_ks_feature_search_by_id(id));
	read_unlock(&ks_features_list_lock);

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

static int ks_feature_write_to_nlmsg(
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

static int ks_feature_mcast_send(
	struct ks_feature *feature,
	struct ks_netlink_state *state,
	enum ks_netlink_message_type message_type)
{
	int err;

retry:
	err = ks_netlink_mcast_need_skb(state);
	if (err < 0)
		return err;

	err = ks_feature_write_to_nlmsg(feature, state->mcast_skb, message_type,
						0, state->mcast_seqnum++, 0);
	if (err < 0) {
		ks_netlink_mcast_flush(state);
		goto retry;
	}

	ks_netlink_mcast_flush(state);

	return 0;
}

int ks_feature_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	int err;
	int i;
	int cnt = 1;
  
	ks_netlink_send_ack(state, nlh, NLM_F_MULTI);

	read_lock(&ks_features_list_lock);
	for(i=0; i<KS_FEATURE_HASHSIZE; i++) {
		struct hlist_node *t;

		struct ks_feature *feature;
		hlist_for_each_entry(feature, t, &ks_features_hash[i], node) {

retry:
			ks_netlink_need_skb(state);
			if (!state->out_skb) {
				read_unlock(&ks_features_list_lock);
				return -ENOMEM;
			}

			err = ks_feature_write_to_nlmsg(feature,
						state->out_skb,
						KS_NETLINK_FEATURE_NEW,
						nlh->nlmsg_pid,
						nlh->nlmsg_seq + cnt,
						NLM_F_MULTI);
			if (err < 0) {
				ks_netlink_flush(state);
				goto retry;
			}

			cnt++;
		}
	}
	read_unlock(&ks_features_list_lock);

	ks_netlink_send_done(state, nlh, cnt);

	return 0;
}

static struct ks_feature *ks_feature_register_no_topology_lock(const char *name)
{
	struct ks_feature *feature;
	int i;

	write_lock(&ks_features_list_lock);
	for(i=0; i<KS_FEATURE_HASHSIZE; i++) {
		struct hlist_node *t;

		hlist_for_each_entry(feature, t, &ks_features_hash[i], node) {
			if (!strcmp(feature->name, name)) {
				write_unlock(&ks_features_list_lock);
				return ks_feature_get(feature);
			}
		}
	}

	feature = kmalloc(sizeof(*feature), GFP_ATOMIC);
	if (!feature) {
		write_unlock(&ks_features_list_lock);
		return NULL;
	}

	memset(feature, 0, sizeof(*feature));

	atomic_set(&feature->refcnt, 1);

	strncpy(feature->name, name, sizeof(feature->name));

	feature->id = _ks_feature_new_id();
	hlist_add_head(&feature->node, ks_features_get_hash(feature->id));

	ks_feature_mcast_send(feature, &ks_netlink_state,
				KS_NETLINK_FEATURE_NEW);

	write_unlock(&ks_features_list_lock);

	return feature;
}

struct ks_feature *ks_feature_register(const char *name)
{
	struct ks_feature *feature;

	down_write(&ks_topology_lock);
	feature = ks_feature_register_no_topology_lock(name);
	up_write(&ks_topology_lock);

	return feature;
}
EXPORT_SYMBOL(ks_feature_register);

void ks_feature_unregister(struct ks_feature *feature)
{
	if (atomic_read(&feature->refcnt) == 1) {
		down_write(&ks_topology_lock);
		hlist_del(&feature->node);

		ks_feature_mcast_send(feature, &ks_netlink_state,
				KS_NETLINK_FEATURE_DEL);
		up_write(&ks_topology_lock);
	}

	ks_feature_put(feature);
}
EXPORT_SYMBOL(ks_feature_unregister);

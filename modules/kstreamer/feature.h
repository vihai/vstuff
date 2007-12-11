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

#ifndef __KS_FEATURE_H
#define __KS_FEATURE_H

#include <linux/types.h>

enum ks_feature_attribute_type
{
	KS_FEATURE_ID = 1,
	KS_FEATURE_NAME,
};

#ifdef __KERNEL__

#include <linux/skbuff.h>

#include "netlink.h"

struct ks_feature
{
	struct hlist_node node;

	atomic_t refcnt;

	u32 id;
	char name[32];
};

struct ks_feature_value
{
	struct list_head node;

	struct ks_feature *feature;

	int len;
};

int ks_feature_cmd_get(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh);

extern struct ks_feature *ks_feature_register(const char *name);
extern void ks_feature_unregister(struct ks_feature *feature);

extern struct ks_feature *ks_feature_get(struct ks_feature *feature);
extern void ks_feature_put(struct ks_feature *feature);

#endif

#endif

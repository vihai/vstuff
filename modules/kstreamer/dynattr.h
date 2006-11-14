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

#ifndef __KS_DYNATTR_H
#define __KS_DYNATTR_H

#include <linux/types.h>

enum ks_dynattr_attribute_type
{
	KS_DYNATTR_ID = 1,
	KS_DYNATTR_NAME,
};

#ifdef __KERNEL__

#include <linux/skbuff.h>

struct ks_xact;

struct ks_dynattr
{
	struct hlist_node node;

	atomic_t refcnt;

	u32 id;
	char name[32];
};

struct ks_dynattr_instance
{
	struct list_head node;

	struct ks_dynattr *dynattr;

	int len;
};

void ks_dynattr_netlink_dump(struct ks_xact *xact);

extern struct ks_dynattr *ks_dynattr_register(const char *name);
extern void ks_dynattr_unregister(struct ks_dynattr *dynattr);

extern struct ks_dynattr *ks_dynattr_get(struct ks_dynattr *dynattr);
extern void ks_dynattr_put(struct ks_dynattr *dynattr);

#endif

#endif

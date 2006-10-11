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
#include <errno.h>
#include <assert.h>

#include <linux/kstreamer/link.h>
#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include "link.h"
#include "node.h"
#include "dynattr.h"
#include "netlink.h"

struct hlist_head ks_links_hash[LINK_HASHSIZE];

static inline struct hlist_head *ks_link_get_hash(int id)
{
	return &ks_links_hash[id & (LINK_HASHSIZE - 1)];
}

const char *ks_netlink_link_attr_to_string(
		enum ks_link_attribute_type type)
{
	switch(type) {
	case KS_LINKATTR_ID:
		return "ID";
	case KS_LINKATTR_PATH:
		return "PATH";
	case KS_LINKATTR_FROM:
		return "FROM";
	case KS_LINKATTR_TO:
		return "TO";
	}

	return "UNKNOWN";
}

void ks_link_add(struct ks_link *link)
{
	hlist_add_head(
		&ks_link_get(link)->node,
		ks_link_get_hash(link->id));
}

void ks_link_del(struct ks_link *link)
{
	hlist_del(&link->node);

	ks_link_put(link);
}

struct ks_link *ks_link_get_by_id(int id)
{
	struct ks_link *link;
	struct hlist_node *t;

	hlist_for_each_entry(link, t, ks_link_get_hash(id), node) {
		if (link->id == id)
			return ks_link_get(link);
	}

	return NULL;
}

struct ks_link *ks_link_get_by_nlid(struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		if(attr->type == KS_LINKATTR_ID)
			return ks_link_get_by_id(*(__u32 *)KS_ATTR_DATA(attr));
	}

	return NULL;
}

struct ks_link *ks_link_alloc(void)
{
	struct ks_link *link;
	
	link = malloc(sizeof(*link));
	if (!link)
		return NULL;
	
	memset(link, 0, sizeof(*link));

	link->refcnt = 1;

	INIT_LIST_HEAD(&link->dynattrs);

	return link;
}

struct ks_link *ks_link_get(struct ks_link *link)
{
	assert(link->refcnt > 0);

	if (link)
		link->refcnt++;

	return link;
}

void ks_link_put(struct ks_link *link)
{
	assert(link->refcnt > 0);

	link->refcnt--;

	if (!link->refcnt) {
		struct ks_dynattr_instance *dynattr, *t;

		list_for_each_entry_safe(dynattr, t, &link->dynattrs, node) {
			list_del(&dynattr->node);
			free(dynattr);
		}

		free(link);
	}
}

struct ks_link *ks_link_create_from_nlmsg(struct nlmsghdr *nlh)
{
	struct ks_link *link;

	link = ks_link_alloc();
	if (!link)
		return NULL;

	ks_link_update_from_nlmsg(link, nlh);

	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		switch(attr->type) {
		case KS_LINKATTR_ID:
			link->id = *(__u32 *)KS_ATTR_DATA(attr);
		break;

		case KS_LINKATTR_PATH:
			if (link->path)
				free(link->path);

			link->path = strndup(KS_ATTR_DATA(attr),
					KS_ATTR_PAYLOAD(attr));
		break;

		case KS_LINKATTR_FROM:
			link->from = ks_node_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		case KS_LINKATTR_TO:
			link->to = ks_node_get_by_id(
					*(__u32 *)KS_ATTR_DATA(attr));
		break;

		default: {
			struct ks_dynattr *dynattr_class;
			dynattr_class = ks_dynattr_get_by_id(attr->type);
			if (!dynattr_class) {
				printf("   Attribute %d unknown\n", attr->type);
				break;
			}

			struct ks_dynattr_instance *dynattr;
			dynattr = malloc(sizeof(*dynattr) + attr->len);
			if (!dynattr) {
				// FIXME
				break;
			}

			dynattr->dynattr = dynattr_class;
			dynattr->len = KS_ATTR_PAYLOAD(attr);
			memcpy(dynattr->payload,
				KS_ATTR_DATA(attr),
				KS_ATTR_PAYLOAD(attr));
			
			list_add(&dynattr->node, &link->dynattrs);
		}
		}
	}

	return link;
}

void ks_link_update_from_nlmsg(struct ks_link *link, struct nlmsghdr *nlh)
{
	struct ks_attr *attr;
	int attrs_len = KS_PAYLOAD(nlh);

	for (attr = KS_ATTRS(nlh);
	     KS_ATTR_OK(attr, attrs_len);
	     attr = KS_ATTR_NEXT(attr, attrs_len)) { 

		switch(attr->type) {
		case KS_LINKATTR_ID:
		case KS_LINKATTR_PATH:
		case KS_LINKATTR_FROM:
		case KS_LINKATTR_TO:
			/* Are updates to these allowed? */
		break;

		default: {
			struct ks_dynattr_instance *dynattr;
			list_for_each_entry(dynattr, &link->dynattrs, node) {
				if (dynattr->dynattr->id == attr->type)
					assert(dynattr->len ==
						KS_ATTR_PAYLOAD(attr));

					memcpy(dynattr->payload,
						KS_ATTR_DATA(attr),
						KS_ATTR_PAYLOAD(attr));

					break;
				}
		}
		}
	}
}

int ks_link_write_to_nlmsg(
	struct ks_link *link,
	struct sk_buff *skb,
	enum ks_netlink_message_type message_type,
	__u32 pid, __u32 seq, __u16 flags)
{
	struct nlmsghdr *nlh;
	void *oldtail;
	int err = -ENOBUFS;

	oldtail = skb->tail;

	nlh = ks_nlmsg_put(skb, pid, seq, message_type, flags, 0);
	if (!nlh)
		goto nlmsg_failure;

	nlh->nlmsg_flags = flags;

	err = ks_netlink_put_attr(skb, KS_LINKATTR_ID,
				&link->id, sizeof(link->id));
	if (err < 0)
		goto err_put_attr;

	if (message_type != KS_NETLINK_LINK_DEL) {

		struct ks_dynattr_instance *dynattr;

		list_for_each_entry(dynattr, &link->dynattrs, node) {

			err = ks_netlink_put_attr(skb, dynattr->dynattr->id,
					dynattr->payload, dynattr->len);
			if (err < 0)
				goto err_put_attr;
		}
	}

	nlh->nlmsg_len = skb->tail - oldtail;

	return 0;

err_put_attr:
nlmsg_failure:
	skb_trim(skb, oldtail - skb->data);

	return err;
}

void ks_link_dump(struct ks_link *link)
{
	printf("  ID    : 0x%08x\n", link->id);
	printf("  Path  : '%s'\n", link->path);

	if (link->from)
		printf("  From  : 0x%08x (%s)\n",
			link->from->id, link->from->path);

	if (link->to)
		printf("  To    : 0x%08x (%s)\n",
			link->to->id, link->to->path);

	struct ks_dynattr_instance *dynattr;
	list_for_each_entry(dynattr, &link->dynattrs, node) {
		printf("  Dynattr: %s\n", dynattr->dynattr->name);

		printf("  Data: ");
		
		int i;
		for(i=0; i<dynattr->len; i++) {
			printf("%02x ",
				*(dynattr->payload + i));
		}

		printf("\n");
	}
}


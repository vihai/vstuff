/*
 * vISDN KSTREAMS/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _FAKE_NETLINK_H
#define _FAKE_NETLINK_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/socket.h>
#else
#include <sys/socket.h>
#endif

#ifndef NETLINK_KSTREAMER
#define NETLINK_KSTREAMER 31
#endif

extern struct sock *ksnl;

#define KS_NETLINK_BASE 16
#define KS_NETLINK_OBJS 32

enum ks_netlink_message_type
{
	KS_NETLINK_VERSION = KS_NETLINK_BASE,
	KS_NETLINK_BEGIN,
	KS_NETLINK_COMMIT,
	KS_NETLINK_ABORT,

	KS_NETLINK_DYNATTR_NEW = KS_NETLINK_OBJS,
	KS_NETLINK_DYNATTR_DEL,
	KS_NETLINK_DYNATTR_GET,
	KS_NETLINK_DYNATTR_SET,

	KS_NETLINK_NODE_NEW,
	KS_NETLINK_NODE_DEL,
	KS_NETLINK_NODE_GET,
	KS_NETLINK_NODE_SET,

	KS_NETLINK_CHAN_NEW,
	KS_NETLINK_CHAN_DEL,
	KS_NETLINK_CHAN_GET,
	KS_NETLINK_CHAN_SET,

	KS_NETLINK_PIPELINE_NEW,
	KS_NETLINK_PIPELINE_DEL,
	KS_NETLINK_PIPELINE_GET,
	KS_NETLINK_PIPELINE_SET,
};

enum ks_netlink_groups
{
	KS_NETLINK_GROUP_TOPOLOGY = 1 << 0,
};

struct ks_netlink_version_response
{
	__u8 major;
	__u8 minor;
	__u8 service;
	__u8 reserved;
};

struct ks_attr
{
        __u16 len;
        __u16 type;

	__u8 payload[0];
};

#define KS_ATTR_ALIGNTO     4
#define KS_ATTR_ALIGN(len) \
	(((len) + KS_ATTR_ALIGNTO - 1) & ~(KS_ATTR_ALIGNTO - 1))

#define KS_ATTR_OK(attr, attrlen) \
	((attrlen) >= (int)sizeof(struct ks_attr) && \
		(attr)->len >= sizeof(struct ks_attr) && \
		(attr)->len <= (attrlen))

#define KS_ATTR_NEXT(attr, attrlen) \
	((attrlen) -= KS_ATTR_ALIGN((attr)->len), \
		(struct ks_attr *)(((char *)(attr)) + \
		KS_ATTR_ALIGN((attr)->len)))

#define KS_ATTR_LENGTH(len) (KS_ATTR_ALIGN(sizeof(struct ks_attr)) + (len))
#define KS_ATTR_SPACE(len) KS_ATTR_ALIGN(KS_ATTR_LENGTH(len))
#define KS_ATTR_DATA(attr) ((void *)(((char *)(attr)) + KS_ATTR_LENGTH(0)))
#define KS_ATTR_PAYLOAD(attr) ((int)((attr)->len) - KS_ATTR_LENGTH(0))

#define KS_ATTRS(r) ((struct ks_attr *)(((char *)(r)) + NLMSG_LENGTH(0)))
#define KS_PAYLOAD(n) NLMSG_PAYLOAD(n, 0)
#define KS_DATA(n) (NLMSG_DATA(n) + 0)

/*#define KS_ATTRS(r) \
	((struct ks_attr *)(((char *)(r)) + \
	NLMSG_ALIGN(sizeof(struct ks_netlink_hdr))))

#define KS_PAYLOAD(n) \
	NLMSG_PAYLOAD(n, sizeof(struct ks_netlink_hdr))*/

#ifdef __KERNEL__
#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/version.h>
#include <net/sock.h>

struct ks_sock
{
	struct sock sk;

};

enum ks_xact_flags
{
	KS_XACT_FLAGS_WRITE,
	KS_XACT_FLAGS_PERSISTENT,
	KS_XACT_FLAGS_COMPLETED,
};

struct ks_xact
{
	struct hlist_node node;

	atomic_t refcnt;

	struct sk_buff *out_skb;

	u32 pid;
	u32 id;
	u16 flags;
};

#define KS_CMD_WR (1 << 0)

struct ks_command
{
	enum ks_netlink_message_type message_type;

	int (*handler)(
		struct ks_command *cmd,
		struct ks_xact *xact,
		struct nlmsghdr *nlh);

	int flags;
};


#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define ks_sock_debug(sock, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG ks_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define ks_sock_debug(sock, format, arg...) do {} while (0)
#endif

#define ks_sock_msg(sock, level, format, arg...)		\
	printk(level ks_MODULE_PREFIX				\
		format,						\
		## arg)

#define to_ks_sock(obj) container_of(obj, struct ks_sock, sk)

int ks_kobj_make_path(struct kobject *kobj, void *buf);
int ks_netlink_put_attr_path(
	struct sk_buff *skb,
	int type,
	struct kobject *kobj);
int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len);

void ks_xact_send_control(struct ks_xact *xact,
		enum ks_netlink_message_type message_type, u16 flags);
void ks_xact_send_error(struct ks_xact *xact, int error);
void ks_xact_need_skb(struct ks_xact *xact);
void ks_xact_flush(struct ks_xact *xact);

int ks_netlink_modinit(void);
void ks_netlink_modexit(void);

#endif
#endif

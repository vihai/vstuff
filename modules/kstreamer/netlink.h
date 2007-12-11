/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_NETLINK_H
#define _KS_NETLINK_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/socket.h>
#else
#include <sys/socket.h>
#endif

#ifndef NETLINK_KSTREAMER
#define NETLINK_KSTREAMER 31
#endif

#define KS_NETLINK_BASE 16
#define KS_NETLINK_OBJS 32

enum ks_netlink_message_type
{
	KS_NETLINK_VERSION = KS_NETLINK_BASE,

	KS_NETLINK_TOPOLOGY_LOCK,
	KS_NETLINK_TOPOLOGY_TRYLOCK,
	KS_NETLINK_TOPOLOGY_UNLOCK,

	KS_NETLINK_FEATURE_NEW = KS_NETLINK_OBJS,
	KS_NETLINK_FEATURE_DEL,
	KS_NETLINK_FEATURE_GET,
	KS_NETLINK_FEATURE_SET,

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

extern struct sock *ksnl;

extern struct semaphore ksnl_sem;

extern struct ks_netlink_state ks_netlink_state;


struct ks_sock
{
	struct sock sk;
};

struct ks_netlink_state
{
	/* These not need locking as they are accessed only by ksnl thread */
	struct sk_buff *out_skb;

	int cur_pid;
	u32 cur_seq;

	int lock_owner;
	struct timer_list lock_timer;

	int mcast_seqnum;
	struct sk_buff *mcast_skb;
	struct sk_buff_head mcast_queue;
};

#define KS_CMD_RD		(1 << 0)
#define KS_CMD_WR		(1 << 1)

struct ks_command
{
	enum ks_netlink_message_type message_type;

	int (*handler)(
		struct ks_netlink_state *state,
		struct ks_command *cmd,
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

int ks_netlink_need_skb(struct ks_netlink_state *state);
int ks_netlink_mcast_need_skb(struct ks_netlink_state *state);
void ks_netlink_flush(struct ks_netlink_state *state);
void ks_netlink_mcast_flush(struct ks_netlink_state *state);

int ks_netlink_send_done(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	u16 seq);
int ks_netlink_send_ack(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	int flags);
int ks_netlink_send_error(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	int error);

int ks_netlink_modinit(void);
void ks_netlink_modexit(void);

#endif
#endif

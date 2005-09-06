/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_H
#define _LAPD_H

#include <linux/types.h>
#include <linux/socket.h>

#ifndef ARPHRD_LAPD
#define ARPHRD_LAPD 1000
#endif

#ifndef ETH_P_LAPD
#define ETH_P_LAPD 0x0030	/* LAPD pseudo type */
#endif

#ifndef AF_LAPD
#define AF_LAPD 30
#endif

#ifndef PF_LAPD
#define PF_LAPD AF_LAPD
#endif

#ifndef ETH_P_LAPD
#define ETH_P_LAPD 0x0030
#endif

#ifndef SOL_LAPD
#define SOL_LAPD 300
#endif

#define LAPD_SAPI_Q931		0x00
#define LAPD_SAPI_X25		0x0f

enum
{
	LAPD_ROLE		= 0,
	LAPD_TEI		= 1,
	LAPD_SAPI		= 2,
	LAPD_TEI_MGMT_STATUS	= 3,
	LAPD_TEI_MGMT_T201	= 4,
	LAPD_TEI_MGMT_N202	= 5,
	LAPD_TEI_MGMT_T202	= 6,
	LAPD_DLC_STATE		= 7,
	LAPD_T200		= 8,
	LAPD_N200		= 9,
	LAPD_T203		= 10,
	LAPD_N201		= 11,
	LAPD_K			= 12,
};

enum lapd_role {
	LAPD_ROLE_TE		= 0,
	LAPD_ROLE_NT		= 1
};

struct sockaddr_lapd {
	sa_family_t	sal_family;
	__u8            sal_bcast;
	char		sal_zero[8];
};

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <net/sock.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LAPD_BROADCAST_TEI	127

#define LAPD_HASHBITS		5
#define LAPD_HASHSIZE		((1 << LAPD_HASHBITS) - 1)

#ifdef SOCK_DEBUGGING
#define lapd_debug_sk(sk, format, arg...)		\
		SOCK_DEBUG(sk,				\
			"lapd: "			\
			"%s "				\
			format,				\
			lapd_sk(sk)->dev?lapd_sk(sk)->dev->name:"",	\
			## arg)
#else
#define lapd_debug_sk(sk, format, arg...)		\
		do { } while (0)
#endif

#define lapd_printk(lvl, format, arg...)		\
	printk(lvl "lapd: "				\
		format,					\
		## arg)

#define lapd_printk_sk(lvl, sk, format, arg...)		\
	printk(lvl "lapd: "				\
		"%s "					\
		format,					\
		lapd_sk(sk)->dev?lapd_sk(sk)->dev->name:"",	\
		## arg)

#define lapd_printk_dev(lvl, dev, format, arg...)	\
	printk(lvl "lapd: "				\
		"%s "					\
		format,					\
		(dev)->name,				\
		## arg)

extern struct hlist_head lapd_hash[LAPD_HASHSIZE];
extern rwlock_t lapd_hash_lock;

enum {
	LAPD_PROTO_UFRAME = 0,
	LAPD_PROTO_IFRAME = 1,
};

// Do not changes these values, user mode binary compatibility needs them
enum lapd_datalink_state
{
	LAPD_DLS_NULL					= 0,
	LAPD_DLS_TEI_UNASSIGNED				= 1,
	LAPD_DLS_AWAITING_TEI				= 2,
	LAPD_DLS_ESTABLISH_AWAITING_TEI			= 3,
	LAPD_DLS_TEI_ASSIGNED				= 4,
	LAPD_DLS_AWAITING_ESTABLISH			= 50,
	LAPD_DLS_AWAITING_REESTABLISH			= 51,
	LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE	= 52,
	LAPD_DLS_AWAITING_RELEASE			= 6,
	LAPD_DLS_LINK_CONNECTION_ESTABLISHED		= 7,
	LAPD_DLS_LISTENING				= 100,
};

enum lapd_format_errors
{
	LAPD_FE_LENGTH,
	LAPD_FE_N201,
	LAPD_FE_UNDEFINED_COMMAND,
	LAPD_FE_I_FIELD_NOT_PERMITTED,
};

struct lapd_sap
{
	atomic_t refcnt;

	// SAP parameters

	int k;

	int N200;
	int N201;

	int T200;
	int T203;
};

struct lapd_device
{
	struct net_device *dev;

	struct lapd_ntme *net_tme;
	struct lapd_sap q931;
	struct lapd_sap x25;
};

static inline struct lapd_device *lapd_dev(struct net_device *dev)
{
	return (struct lapd_device *)dev->atalk_ptr;
}

struct lapd_new_dlc
{
	struct hlist_node node;
	struct sock *sk;
};

static inline struct lapd_sap *lapd_sap_alloc(void)
{
	return kmalloc(sizeof(struct lapd_sap), GFP_ATOMIC);
}

static inline void lapd_sap_hold(
	struct lapd_sap *entity)
{
	atomic_inc(&entity->refcnt);
}

static inline void lapd_sap_put(
	struct lapd_sap *entity)
{
	if (atomic_dec_and_test(&entity->refcnt))
		kfree(entity);
}

struct lapd_opt
{
	struct net_device *dev;

	int nt_mode;

	struct sk_buff_head u_queue;

	int retrans_cnt;

	struct timer_list T200_timer;
	struct timer_list T203_timer;

	u8 v_s;
	u8 v_r;
	u8 v_a;

	enum lapd_datalink_state state;

	int peer_busy;
	int me_busy;
	int rejection_exception;
	int in_timer_recovery;
	// ------------------

	struct lapd_sap *sap;
	struct lapd_utme *usr_tme;
	struct lapd_ntme *net_tme;

	int tei; // Only valid in NT mode
	int sapi;

	struct hlist_head new_dlcs;

	struct net_device *ppp_master_dev;
};

/* WARNING: don't change the layout of the members in lapd_sock! */
struct lapd_sock {
	struct sock sk;
	struct lapd_opt lapd;
};

enum lapd_int_msg_type
{
	LAPD_INT_MDL_ASSIGN_REQUEST,
	LAPD_INT_MDL_REMOVE_REQUEST,
	LAPD_INT_MDL_ERROR_RESPONSE,
};

struct lapd_internal_msg
{
	enum lapd_int_msg_type type;
	int param;
};

static inline struct lapd_opt *lapd_sk(const struct sock *__sk)
{
	return &((struct lapd_sock *)__sk)->lapd;
}

extern void setup_lapd(struct net_device *netdev);

extern struct sock *lapd_new_sock(struct sock *parent_sk, u8 tei, int sapi);

extern int lapd_device_event(struct notifier_block *this,
			unsigned long event, void *ptr);
extern void lapd_frame_reject(struct sock *sk, struct sk_buff *skb,
	enum lapd_format_errors error);
extern int lapd_backlog_rcv(struct sock *sk, struct sk_buff *skb);
extern void lapd_change_state(struct sock *sk, enum lapd_datalink_state newstate);
extern void lapd_mdl_error_response(struct sock *sk);
extern void lapd_mdl_assign_request(struct sock *sk, int tei);
extern void lapd_mdl_remove_request(struct sock *sk);
extern const char *lapd_state_to_text(enum lapd_datalink_state state);
extern void lapd_deliver_internal_message(
	struct sock *sk,
	enum lapd_int_msg_type type,
	int param);

static inline struct hlist_head *lapd_get_hash(struct net_device *dev)
{
	return &lapd_hash[dev->ifindex & (LAPD_HASHSIZE - 1)];
}

#endif
#endif

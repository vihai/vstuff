/*
 * lapd.h
 *
 * Copyright (C) 2004 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#include <linux/types.h>
#include <linux/socket.h>

#ifndef _LAPD_H
#define _LAPD_H

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
	LAPD_DLC_STATUS		= 7,
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
//#include <linux/socket.h>

#include "lapd_proto.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LAPD_BROADCAST_TEI	127

#ifdef SOCK_DEBUGGING
#define lapd_debug_sk(sk, format, arg...)		\
	if ((sk)->sk_debug)				\
		printk(KERN_DEBUG "lapd: "		\
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

extern struct hlist_head lapd_hash;
extern rwlock_t lapd_hash_lock;

enum {
	LAPD_PROTO_UFRAME = 0,
	LAPD_PROTO_IFRAME = 1,
};

enum lapd_datalink_status
{
	LAPD_DLS_LISTENING,
	LAPD_DLS_LINK_CONNECTION_RELEASED,
	LAPD_DLS_LINK_CONNECTION_ESTABLISHED,
	LAPD_DLS_AWAITING_ESTABLISH,
	LAPD_DLS_AWAITING_RELEASE,
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

	// Datalink status
	int N200_cnt;

	struct timer_list T200_timer;
	struct timer_list T203_timer;

	u8 v_s;
	u8 v_r;
	u8 v_a;

	enum lapd_datalink_status status;

	int peer_busy;
	int me_busy;
	int peer_waiting_for_ack;
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

static inline struct lapd_opt *lapd_sk(const struct sock *__sk)
{
	return &((struct lapd_sock *)__sk)->lapd;
}

extern void setup_lapd(struct net_device *netdev);

void lapd_unhash(struct sock *sk);
void lapd_T203_timer(unsigned long data);

static inline u8 lapd_get_tei(struct lapd_opt *lo)
{
	if (lo->nt_mode)
		if (lo->status == LAPD_DLS_LISTENING)
			return LAPD_BROADCAST_TEI;
		else
			return lo->tei;
	else
		return lo->usr_tme->tei;
}

struct sock *lapd_new_sock(struct sock *parent_sk, lapd_tei_t tei, int sapi);

int lapd_device_event(struct notifier_block *this,
			unsigned long event, void *ptr);
void lapd_frame_reject(struct sock *sk, struct sk_buff *skb, int w, int x, int y, int z);
int lapd_backlog_rcv(struct sock *sk, struct sk_buff *skb);

#endif
#endif

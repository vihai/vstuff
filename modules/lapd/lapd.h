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
#include <linux/version.h>
#include <net/sock.h>

#define lapd_MODULE_NAME "lapd"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LAPD_BROADCAST_TEI	127

#define LAPD_HASHBITS		8
#define LAPD_HASHSIZE		((1 << LAPD_HASHBITS) - 1)

#ifdef DEBUG_CODE
#define lapd_debug(format, arg...)			\
		printk(KERN_DEBUG			\
			"lapd: "			\
			format,				\
			## arg)

#define lapd_debug_ls(ls, format, arg...)			\
		SOCK_DEBUG(&ls->sk,				\
			"lapd: "				\
			"%s "					\
			format,					\
			(ls)->dev ? (ls)->dev->name : "",	\
			## arg)

#define lapd_debug_dev(dev, format, arg...)			\
		printk(KERN_DEBUG				\
			"lapd: "				\
			"%s "					\
			format,					\
			(dev)->name ? (dev)->name : "",		\
			## arg)
#else
#define lapd_debug(ls, format, arg...) do { } while (0)
#define lapd_debug_ls(ls, format, arg...) do { } while (0)
#define lapd_debug_dev(ls, format, arg...) do { } while (0)
#endif

#define lapd_msg(lvl, format, arg...)			\
	printk(lvl "lapd: "				\
		format,					\
		## arg)

#define lapd_msg_ls(ls, lvl, format, arg...)		\
	printk(lvl "lapd: "				\
		"%s: "					\
		format,					\
		(ls)->dev ? (ls)->dev->name : "",	\
		## arg)

#define lapd_msg_dev(dev, lvl, format, arg...)		\
	printk(lvl "lapd: "				\
		"%s: "					\
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
	LAPD_DLS_1_TEI_UNASSIGNED			= 1,
	LAPD_DLS_2_AWAITING_TEI				= 2,
	LAPD_DLS_3_ESTABLISH_AWAITING_TEI		= 3,
	LAPD_DLS_4_TEI_ASSIGNED				= 4,
	LAPD_DLS_5_AWAITING_ESTABLISH			= 5,
	LAPD_DLS_6_AWAITING_RELEASE			= 6,
	LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED		= 7,
	LAPD_DLS_8_TIMER_RECOVERY			= 8,
	LAPD_DLS_LISTENING				= 0xFF,
};

enum lapd_mdl_error_indications
{
	LAPD_MDL_ERROR_INDICATION_A = (1 << 0),
	LAPD_MDL_ERROR_INDICATION_B = (1 << 1),
	LAPD_MDL_ERROR_INDICATION_C = (1 << 2),
	LAPD_MDL_ERROR_INDICATION_D = (1 << 3),
	LAPD_MDL_ERROR_INDICATION_E = (1 << 4),
	LAPD_MDL_ERROR_INDICATION_F = (1 << 5),
	LAPD_MDL_ERROR_INDICATION_G = (1 << 6),
	LAPD_MDL_ERROR_INDICATION_H = (1 << 7),
	LAPD_MDL_ERROR_INDICATION_I = (1 << 8),
	LAPD_MDL_ERROR_INDICATION_J = (1 << 9),
	LAPD_MDL_ERROR_INDICATION_K = (1 << 10),
	LAPD_MDL_ERROR_INDICATION_L = (1 << 11),
	LAPD_MDL_ERROR_INDICATION_M = (1 << 12),
	LAPD_MDL_ERROR_INDICATION_N = (1 << 13),
	LAPD_MDL_ERROR_INDICATION_O = (1 << 14),
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
	struct lapd_sock *lapd_sock;
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

struct lapd_sock
{
	struct sock sk;

	struct net_device *dev;

	int nt_mode;

	struct sk_buff_head u_queue;

	int retrans_cnt;

	struct timer_list timer_T200;
	struct timer_list timer_T203;

	u8 v_s;
	u8 v_r;
	u8 v_a;

	enum lapd_datalink_state state;

	int peer_receiver_busy;
	int own_receiver_busy;
	int reject_exception;
	int acknowledge_pending;
	int layer_3_initiated;
	// ------------------

	struct lapd_sap *sap;
	struct lapd_utme *usr_tme;

	int tei;
	int sapi;

	struct hlist_head new_dlcs;
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

#define to_lapd_sock(obj) container_of(obj, struct lapd_sock, sk)

struct lapd_sock *lapd_new_sock(
	struct lapd_sock *parent_lapd_sock,
	u8 tei, int sapi);

void lapd_deliver_internal_message(
	struct lapd_sock *lapd_sock,
	enum lapd_int_msg_type type,
	int param);

void lapd_dl_establish_indication(struct lapd_sock *lapd_sock);
void lapd_dl_establish_confirm(struct lapd_sock *lapd_sock);
void lapd_dl_release_indication(struct lapd_sock *lapd_sock);
void lapd_dl_release_confirm(struct lapd_sock *lapd_sock);

void lapd_mdl_error_indication(
	struct lapd_sock *lapd_sock,
	unsigned long indication);

int lapd_dl_unit_data_indication(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb);
void lapd_dl_data_indication(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb);

static inline struct hlist_head *lapd_get_hash(struct net_device *dev)
{
	return &lapd_hash[dev->ifindex & (LAPD_HASHSIZE - 1)];
}

static inline void lapd_bh_lock_sock(struct lapd_sock *lapd_sock)
{
	bh_lock_sock(&lapd_sock->sk);
}

static inline void lapd_bh_unlock_sock(struct lapd_sock *lapd_sock)
{
	bh_unlock_sock(&lapd_sock->sk);
}

static inline void lapd_lock_sock(struct lapd_sock *lapd_sock)
{
	lock_sock(&lapd_sock->sk);
}

static inline void lapd_release_sock(struct lapd_sock *lapd_sock)
{
	release_sock(&lapd_sock->sk);
}


#endif
#endif

#ifndef LAPD_H
#define LAPD_H

#include <net/sock.h>
#include <asm/atomic.h>

#include "lapd_proto.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

#ifdef __KERNEL__

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LAPD_SAPI_Q931		0x00
#define LAPD_SAPI_X25		0x0f

#define LAPD_BROADCAST_TEI	127

extern struct hlist_head lapd_hash;
extern rwlock_t lapd_hash_lock;

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
		return lo->tei;
	else
		return lo->usr_tme->tei;
}

struct sock *lapd_new_sock(struct sock *parent_sk, lapd_tei_t tei, int sapi);

int lapd_device_event(struct notifier_block *this, unsigned long event,
			    void *ptr);

#endif
#endif

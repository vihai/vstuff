#ifndef _TEI_MGMT_TE_H
#define _TEI_MGMT_TE_H

#include <linux/spinlock.h>
#include <asm/atomic.h>

#include "tei_mgmt.h"

extern struct hlist_head lapd_utme_hash;

struct lapd_utme
{
	struct hlist_node node;

	atomic_t refcnt;

	struct net_device *dev;

	spinlock_t lock;

	// TEI Management SAP Parameters
	int T202;
	int N202;
	//---------

	struct timer_list T202_timer;
	int retrans_cnt;

	enum lapd_tei_status status;

	lapd_tei_t tei;
	u16 tei_request_ri;
	int tei_request_pending;

	void (*destroy)(struct lapd_utme *tme);
};

struct lapd_utme *lapd_utme_alloc(struct net_device *dev);

static inline void lapd_utme_hold(
	struct lapd_utme *tme)
{
	atomic_inc(&tme->refcnt);
}

static inline void lapd_utme_put(
	struct lapd_utme *tme)
{
	if (atomic_dec_and_test(&tme->refcnt)) {
		if (tme->destroy) tme->destroy(tme);

		kfree(tme);
	}
}

extern void lapd_utme_set_static_tei(
	struct lapd_utme *tme, lapd_tei_t tei);

static inline void lapd_utme_reset_timer(
	struct lapd_utme *tme,
	struct timer_list *timer,
	unsigned long expires)
{
	if (!mod_timer(timer, expires))
		lapd_utme_hold(tme);
}

static inline void lapd_utme_stop_timer(
	struct lapd_utme *tme,
	struct timer_list *timer)
{
	if (timer_pending(timer) && del_timer(timer))
		lapd_utme_put(tme);
}

static inline void lapd_utme_state_changed(
	struct lapd_utme *tme)
{
}

int lapd_utme_handle_frame(struct sk_buff *skb);
void lapd_utme_start_tei_request(struct lapd_utme *tme);

#endif

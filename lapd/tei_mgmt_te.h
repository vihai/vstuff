#ifndef _TEI_MGMT_TE_H
#define _TEI_MGMT_TE_H

#include <asm/atomic.h>

#include "tei_mgmt.h"

struct lapd_usr_tei_mgmt_entity
{
	struct hlist_node *node;

	atomic_t refcnt;

	struct net_device *dev;
	wait_queue_head_t waitq;

	// TEI Management SAP Parameters
	int T202;
	int N202;
	//---------

	struct timer_list T202_timer;
	int N202_cnt;

	enum lapd_tei_status status;

	u8 tei;
	u16 tei_request_ri;
	int tei_request_pending;
};

extern struct lapd_usr_tei_mgmt_entity *lapd_usr_tei_mgmt_entity_alloc();

static inline void lapd_usr_tei_mgmt_entity_free(
	struct lapd_usr_tei_mgmt_entity *tme)
{
	kfree(tme);
}

static inline void lapd_usr_tei_mgmt_entity_hold(
	struct lapd_usr_tei_mgmt_entity *tme)
{
	atomic_inc(&tme->refcnt);
}

static inline void lapd_usr_tei_mgmt_entity_put(
	struct lapd_usr_tei_mgmt_entity *tme)
{
	if (atomic_dec_and_test(&tme->refcnt))
		lapd_usr_tei_mgmt_entity_free(tme);
}

extern void lapd_usr_tme_set_static_tei(
	struct lapd_usr_tei_mgmt_entity *tme, int tei);


#endif


#ifndef _TEI_MGMT_NT_H
#define _TEI_MGMT_NT_H

#include "tei_mgmt.h"

#define LAPD_MAX_NUM_TEIS	128

struct lapd_net_tme_terminal_equipment
{
//        struct list_head hash_list;

        u8 tei;
	enum lapd_tei_status status;
};

struct lapd_net_tei_mgmt_entity
{
	struct hlist_node *node;

	atomic_t refcnt;

	struct net_device *dev;
	wait_queue_head_t waitq;

	// TEI Management SAP Parameters
	int T201;
	//---------

	struct timer_list T201_timer;

	int cur_dyn_tei;
//	struct list_head tes;
//	rwlock_t tes_lock;

	int tei_check_outstanding;
	int tei_check_count;
	int tei_check_responses[2];
	int tei_check_tei;

	struct lapd_net_tme_terminal_equipment tes[LAPD_MAX_NUM_TEIS];
};

extern struct lapd_net_tei_mgmt_entity *lapd_net_tei_mgmt_entity_alloc();

static inline void lapd_net_tei_mgmt_entity_free(
	struct lapd_net_tei_mgmt_entity *tme)
{
	kfree(tme);
}

static inline void lapd_net_tei_mgmt_entity_hold(
	struct lapd_net_tei_mgmt_entity *tme)
{
	atomic_inc(&tme->refcnt);
}

static inline void lapd_net_tei_mgmt_entity_put(
	struct lapd_net_tei_mgmt_entity *tme)
{
	if (atomic_dec_and_test(&tme->refcnt))
		lapd_net_tei_mgmt_entity_free(tme);
}

extern void lapd_net_tme_set_static_tei(
	struct lapd_net_tei_mgmt_entity *tme, int tei);


#endif

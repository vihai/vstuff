/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>

#include "chan.h"
#include "cxc.h"

static inline struct hlist_head *visdn_cxc_get_hash(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	return &cxc->entries_hash[
			(unsigned long)chan &
			((1 << VISDN_CXC_HASHBITS) - 1)];
}

static void visdn_cxc_delete_rcu(struct rcu_head *head)
{
	struct visdn_cxc_entry *entry =
		container_of(head, struct visdn_cxc_entry, rcu);

	visdn_chan_put(entry->src);
	visdn_chan_put(entry->dst);

	kfree(entry);
}

int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	int err;
	struct visdn_cxc_entry *cxc_entry;

	cxc_entry = kmalloc(sizeof(*cxc_entry), GFP_KERNEL);
	if (!cxc_entry) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	INIT_RCU_HEAD(&cxc_entry->rcu);

	spin_lock(&cxc->lock);

	cxc_entry->src = chan1;
	visdn_chan_get(chan1);
	cxc_entry->dst = chan2;
	visdn_chan_get(chan2);

	hlist_add_head_rcu(&cxc_entry->node,
			visdn_cxc_get_hash(&visdn_cxc, chan1));

	spin_unlock(&cxc->lock);

	if (chan1->ops->connect_to) {
		err = chan1->ops->connect_to(chan1, chan2, 0);
		if (err < 0)
			goto err_connect;
	}

	if (err == VISDN_CONNECT_BRIDGED) {
		// FIXME
	}

	return 0;

err_connect:
err_kmalloc:

	return err;
}

void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	rcu_read_lock();

	struct visdn_cxc_entry *entry;
	struct hlist_node *tpos;
	hlist_for_each_entry_rcu(entry, tpos,
			visdn_cxc_get_hash(&visdn_cxc, chan),
			node) {

		if (entry->src == chan) {
			if (chan->ops->disconnect)
				chan->ops->disconnect(chan);

			hlist_del_rcu(&entry->node);

			call_rcu(&entry->rcu, visdn_cxc_delete_rcu);
		}
	}

	rcu_read_unlock();
}

struct visdn_chan *visdn_cxc_get_by_src(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	struct visdn_cxc_entry *cxc_entry;

	rcu_read_lock();

	struct hlist_node *tpos;
	hlist_for_each_entry_rcu(cxc_entry, tpos,
			visdn_cxc_get_hash(&visdn_cxc, chan),
			node) {

		if (cxc_entry->src == chan) {
			visdn_chan_get(cxc_entry->dst);
			rcu_read_unlock();

			return cxc_entry->dst;
		}
	}

	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL(visdn_cxc_get_by_src);

void visdn_cxc_init(struct visdn_cxc *cxc)
{
	spin_lock_init(&cxc->lock);

	int i;
 	for (i=0; i<ARRAY_SIZE(cxc->entries_hash); i++)
		INIT_HLIST_HEAD(&cxc->entries_hash[i]);
}

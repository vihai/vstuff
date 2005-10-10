/*
 * vISDN software crossconnector
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>

#include <visdn.h>
#include <chan.h>
#include <cxc.h>

#include "cxc_internal.h"

struct vicxc_internal visdn_int_cxc;
EXPORT_SYMBOL(visdn_int_cxc);

static struct timer_list vsp_timer;

static void vsp_timer_func(unsigned long data)
{
	struct vicxc_internal *cxc = (struct vicxc_internal *)data;

	int i;
	for (i=0; i<ARRAY_SIZE(cxc->cxc.connections_hash); i++) {
		struct hlist_node *tpos;

		rcu_read_lock();

		struct visdn_cxc_connection *cxc_entry;
		hlist_for_each_entry_rcu(cxc_entry, tpos,
				&cxc->cxc.connections_hash[i],
				node) {

			if (cxc_entry->src->pars.framing ==
					VISDN_CHAN_FRAMING_TRANS) {
				int nread;

				if (cxc_entry->src->ops->read) {
					nread = cxc_entry->src->ops->read(
						cxc_entry->src,
						cxc->buf, sizeof(cxc->buf));

printk(KERN_INFO "%s => %s: %d\n",
	cxc_entry->src->cxc_id,
	cxc_entry->dst->cxc_id,
	nread);

					if (cxc_entry->dst->ops->write) {
						cxc_entry->dst->ops->write(
							cxc_entry->dst,
							cxc->buf, nread);
					}
				}
			}
		}

		rcu_read_unlock();
	}

	vsp_timer.expires += HZ / 100;
	add_timer(&vsp_timer);
}

static void vicxc_release(struct visdn_cxc *cxc)
{
	printk(KERN_DEBUG "vicxc_release()\n");
}

struct visdn_cxc_ops vicxc_ops =
{
	.owner		= THIS_MODULE,
	.release	= vicxc_release,
};

static int __init vicxc_init_module(void)
{
	int err;

	memset(&visdn_int_cxc, 0, sizeof(visdn_int_cxc));

	visdn_cxc_init(&visdn_int_cxc.cxc);

	visdn_int_cxc.cxc.ops = &vicxc_ops;
	visdn_int_cxc.cxc.name = "internal-cxc";

	err = visdn_cxc_register(&visdn_int_cxc.cxc);
	if (err < 0)
		goto err_cxc_register;

	init_timer(&vsp_timer);
	vsp_timer.expires = jiffies;
	vsp_timer.function = vsp_timer_func;
	vsp_timer.data = (unsigned long)&visdn_int_cxc;

	add_timer(&vsp_timer);

	return 0;

	visdn_cxc_unregister(&visdn_int_cxc.cxc);
err_cxc_register:

	return err;
}
module_init(vicxc_init_module);

static void __exit vicxc_modexit(void)
{
	del_timer_sync(&vsp_timer);

	visdn_cxc_unregister(&visdn_int_cxc.cxc);
}
module_exit(vicxc_modexit);

MODULE_DESCRIPTION(vicxc_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

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

struct visdn_cxc_internal visdn_int_cxc;
EXPORT_SYMBOL(visdn_int_cxc);

static void visdn_int_cxc_release(struct visdn_cxc *cxc)
{
	printk(KERN_DEBUG "visdn_int_cxc_release()\n");
}

struct visdn_cxc_ops visdn_int_cxc_ops =
{
	.owner		= THIS_MODULE,
	.release	= visdn_int_cxc_release,
};

static int __init visdn_int_cxc_init_module(void)
{
	int err;

	memset(&visdn_int_cxc, 0, sizeof(visdn_int_cxc));

	visdn_cxc_init(&visdn_int_cxc.cxc);

	visdn_int_cxc.cxc.ops = &visdn_int_cxc_ops;
	visdn_int_cxc.cxc.name = "internal-cxc";

	err = visdn_cxc_register(&visdn_int_cxc.cxc);
	if (err < 0)
		goto err_cxc_register;

	return 0;

	visdn_cxc_unregister(&visdn_int_cxc.cxc);
err_cxc_register:

	return err;
}
module_init(visdn_int_cxc_init_module);

static void __exit visdn_int_cxc_modexit(void)
{
	visdn_cxc_unregister(&visdn_int_cxc.cxc);
}
module_exit(visdn_int_cxc_modexit);

MODULE_DESCRIPTION(visdn_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

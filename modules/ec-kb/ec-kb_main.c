/*
 * vISDN echo cancellator module, based on Steve Underwood's spandsp
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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>

#include <visdn/core.h>

#include "ec-kb.h"

/******************************************
 * Module stuff
 ******************************************/

static int __init vecs_init_module(void)
{
	int err;

	printk(KERN_INFO vecs_MODULE_DESCR " loaded\n");
}

module_init(vecs_init_module);

static void __exit vecs_module_exit(void)
{

	printk(KERN_INFO vecs_MODULE_DESCR " unloaded\n");
}

module_exit(vecs_module_exit);

MODULE_DESCRIPTION(vecs_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

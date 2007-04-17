/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/autoconf.h>
#include <linux/module.h>

#include <linux/visdn/cxc.h>

#include "cxc.h"

static void hfc_cxc_release(struct visdn_cxc *cxc)
{
}

static int hfc_cxc_connect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2)
{
	return 0;
}

static void hfc_cxc_disconnect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2)
{
}

static struct visdn_cxc_ops hfc_cxc_ops =
{
	.owner		= THIS_MODULE,
	.release	= hfc_cxc_release,

	.connect	= hfc_cxc_connect,
	.disconnect	= hfc_cxc_disconnect,
};

void hfc_cxc_init(
	struct hfc_cxc *cxc)
{
	visdn_cxc_init(&cxc->visdn_cxc);
	cxc->visdn_cxc.ops = &hfc_cxc_ops;
	cxc->visdn_cxc.name = "cxc";
}

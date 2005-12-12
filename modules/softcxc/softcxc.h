/*
 * vISDN software crossconnector
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_SOFTCXC_H
#define _VISDN_SOFTCXC_H

#include <linux/visdn/cxc.h>

#ifdef __KERNEL__

#define vsc_MODULE_NAME "softcxc"
#define vsc_MODULE_PREFIX vsc_MODULE_NAME ": "
#define vsc_MODULE_DESCR "vISDN soft-crossconnector"

struct vsc_softcxc
{
	struct visdn_cxc cxc;

	u8 buf[1024];

	unsigned long long overhead_cycles;
	unsigned long long overhead;
};

extern struct vsc_softcxc vsc_softcxc;

#endif

#endif

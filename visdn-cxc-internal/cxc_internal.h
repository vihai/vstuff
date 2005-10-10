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

#ifndef _VISDN_INT_CXC_H
#define _VISDN_INT_CXC_H

#include <cxc.h>

#define vicxc_MODULE_NAME "cxc-internal"
#define vicxc_MODULE_PREFIX vicxc_MODULE_NAME ": "
#define vicxc_MODULE_DESCR "vISDN internal crossconnector"

struct vicxc_internal
{
	struct visdn_cxc cxc;

	u8 buf[1024];
};

extern struct vicxc_internal visdn_int_cxc;

#endif

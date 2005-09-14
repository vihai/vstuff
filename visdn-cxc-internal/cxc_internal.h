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

#ifndef _VISDN_INT_CXC_H
#define _VISDN_INT_CXC_H

#include <cxc.h>

#define visdn_MODULE_NAME "cxc-internal"
#define visdn_MODULE_PREFIX visdn_MODULE_NAME ": "
#define visdn_MODULE_DESCR "vISDN internal crossconnector"

struct visdn_cxc_internal
{
	struct visdn_cxc cxc;
};

extern struct visdn_cxc_internal visdn_int_cxc;

#endif

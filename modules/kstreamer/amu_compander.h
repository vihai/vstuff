/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _AMU_COMPANDER_H
#define _AMU_COMPANDER_H

#include <linux/types.h>

struct ks_amu_compander_descr
{
	__u8 hardware:1;
	__u8 enabled:1;
	__u8 mu_mode:1;
	__u32 :30;
};

struct ks_amu_decompander_descr
{
	__u8 hardware:1;
	__u8 enabled:1;
	__u8 mu_mode:1;
	__u32 :30;
};

#endif

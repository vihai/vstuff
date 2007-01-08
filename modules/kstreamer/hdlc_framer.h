/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HDLC_FRAMER_H
#define _HDLC_FRAMER_H

#include <linux/types.h>

struct ks_hdlc_framer_descr
{
	__u8 hardware:1;
	__u8 enabled:1;
	__u32 :30;
};

struct ks_hdlc_deframer_descr
{
	__u8 hardware:1;
	__u8 enabled:1;
	__u32 :30;
};

#endif

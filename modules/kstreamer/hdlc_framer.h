/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HDLC_FRAMER_H
#define _HDLC_FRAMER_H

#define HDLC_FRAMER_FLAG_ENABLED	(1 << 0)
#define HDLC_FRAMER_FLAG_HARDWARE	(1 << 1)

struct hdlc_framer_descr
{
	__u32 flags;
};

#endif

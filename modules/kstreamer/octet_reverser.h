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

#ifndef _OCTET_REVERSER_H
#define _OCTET_REVERSER_H

#define OCTET_REVERSER_FLAG_ENABLED	(1 << 0)
#define OCTET_REVERSER_FLAG_HARDWARE	(1 << 1)

struct octet_reverser_descr
{
	__u32 flags;
};

#endif

/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _NUMBER_H
#define _NUMBER_H

#include "util.h"

enum vgsm_type_of_number
{
	VGSM_TON_UNKNOWN		= 0x0,
	VGSM_TON_INTERNATIONAL		= 0x1,
	VGSM_TON_NATIONAL		= 0x2,
	VGSM_TON_NETWORK_SPECIFIC	= 0x3,
	VGSM_TON_SUBSCRIBER		= 0x4,
	VGSM_TON_ALPHANUMERIC		= 0x5,
	VGSM_TON_ABBREVIATED		= 0x6,
	VGSM_TON_RESERVED		= 0x7,
};

enum vgsm_numbering_plan
{
	VGSM_NP_UNKNOWN			= 0x0,
	VGSM_NP_ISDN			= 0x1,
	VGSM_NP_DATA			= 0x3,
	VGSM_NP_TELEX			= 0x4,
	VGSM_NP_NATIONAL		= 0x8,
	VGSM_NP_PRIVATE			= 0x9,
	VGSM_NP_ERMES			= 0xa,
	VGSM_NP_RESERVED		= 0xf,
};

int vgsm_number_parse(
	const char *num,
	char *addr, int addr_len,
	enum vgsm_numbering_plan *np,
	enum vgsm_type_of_number *ton);

#endif

/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2008 Daniele Orlandi
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

struct vgsm_number
{
	char digits[32];
	enum vgsm_numbering_plan np;
	enum vgsm_type_of_number ton;
};

int vgsm_number_parse(
	struct vgsm_number *number,
	const char *string);

void vgsm_number_copy(struct vgsm_number *dst, const struct vgsm_number *src);
const char *vgsm_number_prefix(struct vgsm_number *number);

const char *vgsm_type_of_number_to_text(enum vgsm_type_of_number ton);
const char *vgsm_numbering_plan_to_text(enum vgsm_numbering_plan np);

#endif

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

#include <stdio.h>
#include <string.h>

#include "util.h"
#include "number.h"

int vgsm_number_parse(
	const char *num,
	char *addr, int addr_len,
	enum vgsm_numbering_plan *np,
	enum vgsm_type_of_number *ton)
{
	// FIXME TODO: Better validity checking

	assert(num);
	assert(addr);
	assert(np);
	assert(ton);

	*np = VGSM_NP_ISDN;

	if (num[0] == '+') {
		strncpy(addr, num + 1, addr_len);
		*ton = VGSM_TON_INTERNATIONAL;
	} else {
		strncpy(addr, num, addr_len);
		*ton = VGSM_TON_NATIONAL;
	}

	return 0;
}


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
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "number.h"

const char *vgsm_type_of_number_to_text(enum vgsm_type_of_number ton)
{
	switch(ton) {
	case VGSM_TON_UNKNOWN:
		return "Unknown";
	case VGSM_TON_INTERNATIONAL:
		return "International";
	case VGSM_TON_NATIONAL:
		return "National";
	case VGSM_TON_NETWORK_SPECIFIC:
		return "Network specific";
	case VGSM_TON_SUBSCRIBER:
		return "Subscriber";
	case VGSM_TON_ALPHANUMERIC:
		return "Alphanumeric";
	case VGSM_TON_ABBREVIATED:
		return "Abbreviated";
	case VGSM_TON_RESERVED:
		return "Reserved";
	default:
		return "*UNKNOWN*";
	}
}

const char *vgsm_numbering_plan_to_text(enum vgsm_numbering_plan np)
{
	switch(np) {
	case VGSM_NP_UNKNOWN:
		return "Unknown";
	case VGSM_NP_ISDN:
		return "ISDN telephony";
	case VGSM_NP_DATA:
		return "Data";
	case VGSM_NP_TELEX:
		return "Telex";
	case VGSM_NP_NATIONAL:
		return "National";
	case VGSM_NP_PRIVATE:
		return "Private";
	case VGSM_NP_ERMES:
		return "ERMES";
	case VGSM_NP_RESERVED:
		return "Reserved";
	default:
		return "*UNKNOWN*";
	}
}

int vgsm_number_parse(
	struct vgsm_number *number,
	const char *string)
{
	// FIXME TODO: Better validity checking

	assert(number);
	assert(string);

	number->np = VGSM_NP_ISDN;

	if (string[0] == '+') {
		strncpy(number->digits, string + 1, sizeof(number->digits));
		number->ton = VGSM_TON_INTERNATIONAL;
	} else {
		strncpy(number->digits, string, sizeof(number->digits));
		number->ton = VGSM_TON_NATIONAL;
	}

	return 0;
}

void vgsm_number_copy(struct vgsm_number *dst, const struct vgsm_number *src)
{
	assert(dst);
	assert(src);

	memcpy(dst, src, sizeof(*dst));
}

const char *vgsm_number_prefix(struct vgsm_number *number)
{
	/* We probably need to handle national numbers but they are
	 * currently unused in our test networks
	 */

	if ((number->np == VGSM_NP_ISDN || number->np == VGSM_NP_UNKNOWN) &&
	    number->ton == VGSM_TON_INTERNATIONAL)
		return "+";
	else
		return "";
}

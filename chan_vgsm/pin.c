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

#include <ctype.h>

#include "pin.h"

BOOL vgsm_pin_valid(const char *pin)
{
	int i;

	for (i=0; i<strlen(pin); i++) {
		if (!isdigit(pin[i]))
			return FALSE;
	}

	return TRUE;
}


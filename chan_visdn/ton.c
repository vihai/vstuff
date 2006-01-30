/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <string.h>

#include <asterisk/logger.h>

#include "ton.h"

enum visdn_type_of_number visdn_ton_from_string(const char *str)
{
	if (!strcasecmp(str, "unknown"))
		return VISDN_TYPE_OF_NUMBER_UNKNOWN;
	else if (!strcasecmp(str, "international"))
		return VISDN_TYPE_OF_NUMBER_INTERNATIONAL;
	else if (!strcasecmp(str, "national"))
		return VISDN_TYPE_OF_NUMBER_NATIONAL;
	else if (!strcasecmp(str, "network_specific"))
		return VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC;
	else if (!strcasecmp(str, "subscriber"))
		return VISDN_TYPE_OF_NUMBER_SUBSCRIBER;
	else if (!strcasecmp(str, "abbreviated"))
		return VISDN_TYPE_OF_NUMBER_ABBREVIATED;
	else {
		ast_log(LOG_ERROR,
			"Unknown type_of_number '%s'\n",
			str);

		return VISDN_TYPE_OF_NUMBER_UNKNOWN;
	}
}

const char *visdn_ton_to_string(
	enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNSET:
		return "unset";
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return "unknown";
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return "international";
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return "national";
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return "network specific";
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return "subscriber";
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return "private";
	}

	return "*UNKNOWN*";
}


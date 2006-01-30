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

#ifndef _TON_H
#define _TON_H

enum visdn_type_of_number
{
	VISDN_TYPE_OF_NUMBER_UNSET,
	VISDN_TYPE_OF_NUMBER_UNKNOWN,
	VISDN_TYPE_OF_NUMBER_INTERNATIONAL,
	VISDN_TYPE_OF_NUMBER_NATIONAL,
	VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC,
	VISDN_TYPE_OF_NUMBER_SUBSCRIBER,
	VISDN_TYPE_OF_NUMBER_ABBREVIATED,
};

enum visdn_type_of_number visdn_ton_from_string(const char *str);
const char *visdn_ton_to_string(
	enum visdn_type_of_number type_of_number);

#endif

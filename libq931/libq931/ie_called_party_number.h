/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_IE_CALLED_PARTY_NUMBER_H
#define _LIBQ931_IE_CALLED_PARTY_NUMBER_H

#include <libq931/ie.h>

/*********************** Called Party Number *************************/

enum q931_ie_called_party_number_type_of_number
{
	Q931_IE_CDPN_TON_UNKNOWN		= 0x0,
	Q931_IE_CDPN_TON_INTERNATIONAL		= 0x1,
	Q931_IE_CDPN_TON_NATIONAL		= 0x2,
	Q931_IE_CDPN_TON_NETWORK_SPECIFIC	= 0x3,
	Q931_IE_CDPN_TON_SUBSCRIBER		= 0x4,
	Q931_IE_CDPN_TON_ABBREVIATED		= 0x6,
	Q931_IE_CDPN_TON_RESERVED_FOR_EXT	= 0x7
};

enum q931_ie_called_party_number_numbering_plan_identificator
{
	Q931_IE_CDPN_NPI_UNKNOWN		= 0x0,
	Q931_IE_CDPN_NPI_ISDN_TELEPHONY		= 0x1,
	Q931_IE_CDPN_NPI_DATA			= 0x3,
	Q931_IE_CDPN_NPI_TELEX			= 0x4,
	Q931_IE_CDPN_NPI_NATIONAL_STANDARD	= 0x8,
	Q931_IE_CDPN_NPI_PRIVATE		= 0x9,
	Q931_IE_CDPN_NPI_RESERVED_FOR_EXT	= 0xf
};

struct q931_ie_called_party_number
{
	struct q931_ie ie;

	enum q931_ie_called_party_number_type_of_number
		type_of_number;
	enum q931_ie_called_party_number_numbering_plan_identificator
		numbering_plan_identificator;

	char number[21];
};

struct q931_ie_called_party_number *q931_ie_called_party_number_alloc(void);
struct q931_ie *q931_ie_called_party_number_alloc_abstract(void);

int q931_ie_called_party_number_read_from_buf(
	struct q931_ie *abstract_ie,
	void *buf,
	int len,
	void (*report_func)(int level, const char *format, ...),
	struct q931_interface *intf);

int q931_ie_called_party_number_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size);

void q931_ie_called_party_number_dump(
	const struct q931_ie *ie,
	void (*report)(int level, const char *format, ...),
	const char *prefix);

#ifdef Q931_PRIVATE

struct q931_ie_called_party_number_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 type_of_number:3;
	__u8 numbering_plan_identificator:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 numbering_plan_identificator:4;
	__u8 type_of_number:3;
	__u8 ext:1;
#endif
} __attribute__ ((__packed__));

void q931_ie_called_party_number_register(
	const struct q931_ie_class *ie_class);

#endif
#endif

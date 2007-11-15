/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "../config.h"

#include <asterisk/config.h>

#include "util.h"
#include "bcd.h"

__u8 vgsm_nibbles_to_decimal(__u8 val)
{
	return (val & 0x0f) * 10 + ((val & 0xf0) >> 4);
}

__u8 vgsm_decimal_to_nibbles(__u8 val)
{
	return (val / 10) | ((val % 10) << 4);
}

#if 0
static __u8 swap_nibbles(__u8 val)
{
        return (val & 0xf0 ) >> 4 | (val & 0x0f) << 4;
}
#endif 

int vgsm_bcd_to_text(
	__u8 *buf, int nibbles,
	char *str, int str_len)
{
	int pos = 0;
	int i;

	for(i=0; i<nibbles; i++) {

		__u8 bcd;
		if (i % 2)
			bcd = (*(buf + (i / 2)) & 0xf0) >> 4;
		else
			bcd = *(buf + (i / 2)) & 0x0f;

		if (bcd != 0xf) {
			if (pos >= str_len - 1)
				return -1;

			str[pos++] = vgsm_bcd_to_char(bcd);
		}
	}

	str[pos] = '\0';

	return pos;
}

int vgsm_text_to_bcd(__u8 *buf, char *str)
{
	int i;
	int nibbles = strlen(str);
	int octets = (nibbles + 1) / 2;

	for(i=0; i < octets; i++) {

		if (i * 2 < (nibbles - 1))
			buf[i] = vgsm_char_to_bcd(str[i * 2]) |
				vgsm_char_to_bcd(str[i * 2 + 1]) << 4;
		else
			buf[i] = vgsm_char_to_bcd(str[i * 2]) | 0xf0;
	}

	return nibbles;
}

char vgsm_bcd_to_char(__u8 bcd)
{
	switch(bcd) {
	case 0xa:
		return '*';
	case 0xb:
		return '#';
	case 0xc:
		return 'a';
	case 0xd:
		return 'b';
	case 0xe:
		return 'c';
	case 0xf:
		return 'd'; // never used as 0xf is the terminator
	break;

	default:
		return '0' + bcd;
	}
}

__u8 vgsm_char_to_bcd(char c)
{
	switch(c) {
	case '*':
		return 0xa;
	case '#':
		return 0xb;
	case 'a':
		return 0xc;
	case 'b':
		return 0xd;
	case 'c':
		return 0xe;
	case 'd':
		return 0xf; // never used as 0xf is the terminator
	break;

	default:
		return c - '0';
	}
}


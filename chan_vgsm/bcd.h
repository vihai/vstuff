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

#ifndef _BCD_H
#define _BCD_H

__u8 vgsm_nibbles_to_decimal(__u8 val);
__u8 vgsm_decimal_to_nibbles(__u8 val);

int vgsm_bcd_to_text(
	__u8 *buf, int nibbles,
	char *str, int str_len);
int vgsm_text_to_bcd(__u8 *buf, char *str);

char vgsm_bcd_to_char(__u8 bcd);
__u8 vgsm_char_to_bcd(char c);

#endif

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

#ifndef _7BIT_H
#define _7BIT_H

void vgsm_7bit_to_wc(const __u8 *buf, int septets, int offset,
			wchar_t *out, int outsize);
int vgsm_wc_to_7bit(const wchar_t *in, int inlen, __u8 *out,
			int max_septets, int offset);

int vgsm_octets_to_septets(int octets);
int vgsm_septets_to_octets(int septets);

#endif

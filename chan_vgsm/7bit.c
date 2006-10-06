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

#include "util.h"
#include "7bit.h"
#include "gsm_charset.h"

void vgsm_7bit_to_wc(const __u8 *buf, int septets, wchar_t *out, int outsize)
{
	int i;
	int outlen = 0;

	for(i=0; (i < outsize - 1) && (i < septets); i++) {
		int j = ((i+1)*7)/8;

		int shift = 8 - (i % 8);
		__u16 mask = 0x7f << shift;
		__u16 val = (j ? (*(buf + j-1)) : 0) |
		       	*(buf + j) << 8;

		out[i] = vgsm_gsm_to_wc((val & mask) >> shift);
		outlen++;

#if 0
		ast_verbose("i=%d j=%d", i, j);
		int k;
		ast_verbose(" WIND=");
		for (k=15; k>=0; k--)
			ast_verbose("%c", (mask & (1 << k)) ? '1' : '0');
		ast_verbose(" >> %d 0x%04x => 0x%08x\n", shift, val, out[i]);
#endif
	}

	out[outlen] = L'\0';
}

void vgsm_write_septet(__u8 *out, int septet, char c)
{
	int outpos = ((septet + 1) * 7) / 8;
	int shift = (septet % 8);

	if (outpos > 0) {
		out[outpos-1] |= (c << (8 - shift)) & 0xff;
		out[outpos] |= c >> shift;
	} else {
		out[outpos] |= c;
	}
}

int vgsm_wc_to_7bit(const wchar_t *in, int inlen, __u8 *out)
{
	int i;
	int octets = ((inlen + 1) * 7) / 8;

	for(i=0; i < octets; i++)
		out[i] = 0x00;

	int inpos = 0;
	int outsep = 0;
	while(inpos < inlen) {
		__u8 c, c2;
		wchar_t inwc = in[inpos++];
		int cnt = vgsm_wc_to_gsm(inwc, &c, &c2);

		if (cnt < 1) {
			ast_log(LOG_NOTICE, "Cannot translate char %08x\n",
				(int)inwc);

			continue;
		}

		vgsm_write_septet(out, outsep, c);
		outsep++;

		if (c2) {
			vgsm_write_septet(out, outsep, c2);
			outsep++;
		}
	}

	return octets;
}

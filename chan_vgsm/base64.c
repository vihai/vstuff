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
 * Ispired from http://ftp.sunet.se/pub2/gnu/vm/base64-encode.c :)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"

static const int base64_value[256] = {
	64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1,  0, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

void base64_decode(const __u8 *src, char *dest, int dest_size)
{
	assert(src);

	const __u8 *data = src;

	int noctets = 0;
	__u32 bits = 0;

	int outpos;
	for (outpos = 0; *data && outpos + 4 < dest_size; data++) {

		if (base64_value[*data] < 0)
			continue;

		bits <<= 6;
		bits += base64_value[*data];

		noctets++;

		if (noctets < 4)
			continue;

		/* One quantum of four encoding characters/24 bit */
		dest[outpos++] = (bits >> 16) & 0xff;	/* High 8 bits */
		dest[outpos++] = (bits >> 8) & 0xff;	/* Mid 8 bits */
		dest[outpos++] = bits & 0xff;	/* Low 8 bits */

		bits = 0;
		noctets = 0;
	}

	dest[outpos] = 0;
}

/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 *
 * Tom St Denis, tomstdenis@gmail.com, http://libtomcrypt.com
 */

static const char codes[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const __u8 *in,  int inlen,
                        char *out, int *outlen)
{
	unsigned long i, len2, leven;
	char *p;
	
	assert(in);
	assert(out);
	assert(outlen);
	
	/* valid output size ? */
	len2 = 4 * ((inlen + 2) / 3);
	
	if (*outlen < len2 + 1) {
	     	*outlen = len2 + 1;
	     	return -ENOSPC;
	}

	p = out;
	leven = 3 * (inlen / 3);

	for (i = 0; i < leven; i += 3) {
		*p++ = codes[(in[0] >> 2) & 0x3F];
		*p++ = codes[(((in[0] & 3) << 4) + (in[1] >> 4)) & 0x3F];
		*p++ = codes[(((in[1] & 0xf) << 2) + (in[2] >> 6)) & 0x3F];
		*p++ = codes[in[2] & 0x3F];
		in += 3;
	}
	/* Pad it if necessary...  */
	if (i < inlen) {
		__u8 a = in[0];
		__u8 b = (i+1 < inlen) ? in[1] : 0;
		
		*p++ = codes[(a >> 2) & 0x3F];
		*p++ = codes[(((a & 3) << 4) + (b >> 4)) & 0x3F];
		*p++ = (i+1 < inlen) ? codes[(((b & 0xf) << 2)) & 0x3F] : '=';
		*p++ = '=';
	}
	
	/* append a NULL byte */
	*p = '\0';
	
	/* return ok */
	*outlen = (void *)p - (void *)out;

	return 0;
}

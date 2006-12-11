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
 * Ispired from http://ftp.sunet.se/pub2/gnu/vm/base64-encode.c :)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

static const char base64_code[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

void base64_encode(const __u8 *src, char *dest, int dest_size)
{
	__u32 bits = 0;
	int char_count = 0;
	int out_cnt = 0;
	__u8 c;

	assert(src);
	assert(dest);
	assert(dest_size > 0);

	while ((c = *src++) && out_cnt < dest_size - 5) {

		bits += c;

		char_count++;

		if (char_count == 3) {
			dest[out_cnt++] = base64_code[bits >> 18];
			dest[out_cnt++] = base64_code[(bits >> 12) & 0x3f];
			dest[out_cnt++] = base64_code[(bits >> 6) & 0x3f];
			dest[out_cnt++] = base64_code[bits & 0x3f];

			bits = 0;
			char_count = 0;
		} else {
			bits <<= 8;
		}
	}

	if (char_count != 0) {
		bits <<= 16 - (8 * char_count);

		dest[out_cnt++] = base64_code[bits >> 18];
		dest[out_cnt++] = base64_code[(bits >> 12) & 0x3f];

		if (char_count == 1) {
			dest[out_cnt++] = '=';
			dest[out_cnt++] = '=';
		} else {
			dest[out_cnt++] = base64_code[(bits >> 6) & 0x3f];
			dest[out_cnt++] = '=';
		}
	}

	dest[out_cnt] = '\0';
}

#if 0
const char *base64_encode_bin(const char *data, int len)
{
	static char result[BASE64_RESULT_SZ];
	int bits = 0;
	int char_count = 0;
	int out_cnt = 0;

	if (!data)
	return data;

	while (len-- && out_cnt < sizeof(result) - 5) {
	int c = (unsigned char) *data++;
	bits += c;
	char_count++;
	if (char_count == 3) {
		result[out_cnt++] = base64_code[bits >> 18];
		result[out_cnt++] = base64_code[(bits >> 12) & 0x3f];
		result[out_cnt++] = base64_code[(bits >> 6) & 0x3f];
		result[out_cnt++] = base64_code[bits & 0x3f];
		bits = 0;
		char_count = 0;
	} else {
		bits <<= 8;
	}
	}
	if (char_count != 0) {
	bits <<= 16 - (8 * char_count);
	result[out_cnt++] = base64_code[bits >> 18];
	result[out_cnt++] = base64_code[(bits >> 12) & 0x3f];
	if (char_count == 1) {
		result[out_cnt++] = '=';
		result[out_cnt++] = '=';
	} else {
		result[out_cnt++] = base64_code[(bits >> 6) & 0x3f];
		result[out_cnt++] = '=';
	}
	}
	result[out_cnt] = '\0';	/* terminate */
	return result;
}
#endif

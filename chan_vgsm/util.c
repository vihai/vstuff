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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/time.h>

#include <asterisk/logger.h>

#include "util.h"

longtime_t longtime_now()
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}

wchar_t *w_unprintable_remove(wchar_t *dst, const wchar_t *src, int dst_size)
{
	const wchar_t *s = src;
	wchar_t *d = dst;

	while(*s) {
		if (iswprint(*s)) {
			if (d >= dst + dst_size - 2)
				break;

			*d = *s;
			d++;
		}

		s++;
	}

	*d = L'\0';

	return dst;
}

char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;

	assert(bufsize);

	buf[0] = '\0';

	while(*c) {

		switch(*c) {
		case '\r':
			sanprintf(buf, bufsize, "<cr>");
		break;
		case '\n':
			sanprintf(buf, bufsize, "<lf>");
		break;

		default:
			if (isprint(*c))
				sanprintf(buf, bufsize, "%c", *c);
			else
				sanprintf(buf, bufsize, "<%02x>",
					*(unsigned char *)c);
		}

		c++;
	}

	return buf;
}

__u8 swap_nibbles(__u8 val)
{
	return (val & 0xf0 ) >> 4 | (val & 0x0f) << 4;
}

int char_to_hexdigit(char c)
{
	switch(c) {
		case '0': return 0;
		case '1': return 1;
		case '2': return 2;
		case '3': return 3;
		case '4': return 4;
		case '5': return 5;
		case '6': return 6;
		case '7': return 7;
		case '8': return 8;
		case '9': return 9;
		case 'a': return 10;
		case 'b': return 11;
		case 'c': return 12;
		case 'd': return 13;
		case 'e': return 14;
		case 'f': return 15;
		case 'A': return 10;
		case 'B': return 11;
		case 'C': return 12;
		case 'D': return 13;
		case 'E': return 14;
		case 'F': return 15;
	}

	return -1;
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

__u8 nibbles_to_decimal(__u8 val)
{
	return (val & 0x0f) * 10 + ((val & 0xf0) >> 4);
}

__u8 decimal_to_nibbles(__u8 val)
{
	return (val / 10) | ((val % 10) << 4);
}

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

struct vgsm_7bit_wc_translation
{
	char c;
	char c2;
	wchar_t wc;
} vgsm_7bit_wc_translations[] = {
	{ 0x00,    0, 0x00000064 }, // COMMERCIAL AT
	{ 0x01,    0, 0x000000A3 }, // POUND SIGN
	{ 0x02,    0, 0x00000024 }, // DOLLAR SIGN
	{ 0x03,    0, 0x000000A5 }, // YEN SIGN
	{ 0x04,    0, 0x000000E8 }, // LATIN SMALL LETTER E WITH GRAVE
	{ 0x05,    0, 0x000000E9 }, // LATIN SMALL LETTER E WITH ACUTE
	{ 0x06,    0, 0x000000F9 }, // LATIN SMALL LETTER U WITH GRAVE
	{ 0x07,    0, 0x000000EC }, // LATIN SMALL LETTER I WITH GRAVE
	{ 0x08,    0, 0x000000F2 }, // LATIN SMALL LETTER O WITH GRAVE
	{ 0x09,    0, 0x000000C7 }, // LATIN CAPITAL LETTER C WITH CEDILLA
	{ 0x0A,    0, 0x0000000A }, // --LINE FEED--
	{ 0x0B,    0, 0x000000D8 }, // LATIN CAPITAL LETTER O WITH STROKE
	{ 0x0C,    0, 0x000000F8 }, // LATIN SMALL LETTER O WITH STROKE
	{ 0x0D,    0, 0x0000000D }, // --CARRIAGE RETURN--
	{ 0x0E,    0, 0x000000C5 }, // LATIN CAPITAL LETTER A WITH RING ABOVE
	{ 0x0F,    0, 0x000000E5 }, // LATIN SMALL LETTER A WITH RING ABOVE
	{ 0x10,    0, 0x00000394 }, // GREEK CAPITAL LETTER DELTA
	{ 0x11,    0, 0x0000005F }, // LOW LINE
	{ 0x12,    0, 0x000003A6 }, // GREEK CAPITAL LETTER PHI
	{ 0x13,    0, 0x00000393 }, // GREEK CAPITAL LETTER GAMMA
	{ 0x14,    0, 0x0000039B }, // GREEK CAPITAL LETTER LAMBDA
	{ 0x15,    0, 0x000003A9 }, // GREEK CAPITAL LETTER OMEGA
	{ 0x16,    0, 0x000003A0 }, // GREEK CAPITAL LETTER PI
	{ 0x17,    0, 0x000003A8 }, // GREEK CAPITAL LETTER PSI
	{ 0x18,    0, 0x000003A3 }, // GREEK CAPITAL LETTER SIGMA
	{ 0x19,    0, 0x00000398 }, // GREEK CAPITAL LETTER THETA
	{ 0x1A,    0, 0x0000039E }, // GREEK CAPITAL LETTER XI
	{ 0x1B, 0x0A, 0x0000000C }, // FORM FEED
	{ 0x1B, 0x14, 0x0000005E }, // CIRCUMFLEX ACCENT
	{ 0x1B, 0x28, 0x0000007B }, // LEFT CURLY BRACKET
	{ 0x1B, 0x29, 0x0000007D }, // RIGHT CURLY BRACKET
	{ 0x1B, 0x2F, 0x0000005C }, // REVERSE SOLIDUS (BACKSLASH)
	{ 0x1B, 0x3C, 0x0000005B }, // LEFT SQUARE BRACKET
	{ 0x1B, 0x3D, 0x0000007E }, // TILDE
	{ 0x1B, 0x3E, 0x0000005D }, // RIGHT SQUARE BRACKET
	{ 0x1B, 0x40, 0x0000007C }, // VERTICAL LINE
	{ 0x1B, 0x65, 0x000020AC }, // EURO SIGN
	{ 0x1C,    0, 0x000000C6 }, // LATIN CAPITAL LETTER AE
	{ 0x1D,    0, 0x000000E6 }, // LATIN SMALL LETTER AE
	{ 0x1E,    0, 0x000000DF }, // LATIN SMALL LETTER SHARP S (German)
	{ 0x1F,    0, 0x000000C9 }, // LATIN CAPITAL LETTER E WITH ACUTE
	{ 0x20,    0, 0x00000020 }, // SPACE
	{ 0x21,    0, 0x00000021 }, // EXCLAMATION MARK
	{ 0x22,    0, 0x00000022 }, // QUOTATION MARK
	{ 0x23,    0, 0x00000023 }, // NUMBER SIGN
	{ 0x24,    0, 0x000000A4 }, // CURRENCY SIGN
	{ 0x25,    0, 0x00000025 }, // PERCENT SIGN
	{ 0x26,    0, 0x00000026 }, // AMPERSAND
	{ 0x27,    0, 0x00000027 }, // APOSTROPHE
	{ 0x28,    0, 0x00000028 }, // LEFT PARENTHESIS
	{ 0x29,    0, 0x00000029 }, // RIGHT PARENTHESIS
	{ 0x2A,    0, 0x0000002A }, // ASTERISK
	{ 0x2B,    0, 0x0000002B }, // PLUS SIGN
	{ 0x2C,    0, 0x0000002C }, // COMMA
	{ 0x2D,    0, 0x0000002D }, // HYPHEN-MINUS
	{ 0x2E,    0, 0x0000002E }, // FULL STOP
	{ 0x2F,    0, 0x0000002F }, // SOLIDUS (SLASH)
	{ 0x30,    0, 0x00000030 }, // DIGIT ZERO
	{ 0x31,    0, 0x00000031 }, // DIGIT ONE
	{ 0x32,    0, 0x00000032 }, // DIGIT TWO
	{ 0x33,    0, 0x00000033 }, // DIGIT THREE
	{ 0x34,    0, 0x00000034 }, // DIGIT FOUR
	{ 0x35,    0, 0x00000035 }, // DIGIT FIVE
	{ 0x36,    0, 0x00000036 }, // DIGIT SIX
	{ 0x37,    0, 0x00000037 }, // DIGIT SEVEN
	{ 0x38,    0, 0x00000038 }, // DIGIT EIGHT
	{ 0x39,    0, 0x00000039 }, // DIGIT NINE
	{ 0x3A,    0, 0x0000003A }, // COLON
	{ 0x3B,    0, 0x0000003B }, // SEMICOLON
	{ 0x3C,    0, 0x0000003C }, // LESS-THAN SIGN
	{ 0x3D,    0, 0x0000003D }, // EQUALS SIGN
	{ 0x3E,    0, 0x0000003E }, // GREATER-THAN SIGN
	{ 0x3F,    0, 0x0000003F }, // QUESTION MARK
	{ 0x40,    0, 0x000000A1 }, // INVERTED EXCLAMATION MARK
	{ 0x41,    0, 0x00000041 }, // LATIN CAPITAL LETTER A
	{ 0x42,    0, 0x00000042 }, // LATIN CAPITAL LETTER B
	{ 0x43,    0, 0x00000043 }, // LATIN CAPITAL LETTER C
	{ 0x44,    0, 0x00000044 }, // LATIN CAPITAL LETTER D
	{ 0x45,    0, 0x00000045 }, // LATIN CAPITAL LETTER E
	{ 0x46,    0, 0x00000046 }, // LATIN CAPITAL LETTER F
	{ 0x47,    0, 0x00000047 }, // LATIN CAPITAL LETTER G
	{ 0x48,    0, 0x00000048 }, // LATIN CAPITAL LETTER H
	{ 0x49,    0, 0x00000049 }, // LATIN CAPITAL LETTER I
	{ 0x4A,    0, 0x0000004A }, // LATIN CAPITAL LETTER J
	{ 0x4B,    0, 0x0000004B }, // LATIN CAPITAL LETTER K
	{ 0x4C,    0, 0x0000004C }, // LATIN CAPITAL LETTER L
	{ 0x4D,    0, 0x0000004D }, // LATIN CAPITAL LETTER M
	{ 0x4E,    0, 0x0000004E }, // LATIN CAPITAL LETTER N
	{ 0x4F,    0, 0x0000004F }, // LATIN CAPITAL LETTER O
	{ 0x50,    0, 0x00000050 }, // LATIN CAPITAL LETTER P
	{ 0x51,    0, 0x00000051 }, // LATIN CAPITAL LETTER Q
	{ 0x52,    0, 0x00000052 }, // LATIN CAPITAL LETTER R
	{ 0x53,    0, 0x00000053 }, // LATIN CAPITAL LETTER S
	{ 0x54,    0, 0x00000054 }, // LATIN CAPITAL LETTER T
	{ 0x55,    0, 0x00000055 }, // LATIN CAPITAL LETTER U
	{ 0x56,    0, 0x00000056 }, // LATIN CAPITAL LETTER V
	{ 0x57,    0, 0x00000057 }, // LATIN CAPITAL LETTER W
	{ 0x58,    0, 0x00000058 }, // LATIN CAPITAL LETTER X
	{ 0x59,    0, 0x00000059 }, // LATIN CAPITAL LETTER Y
	{ 0x5A,    0, 0x0000005A }, // LATIN CAPITAL LETTER Z
	{ 0x5B,    0, 0x000000C4 }, // LATIN CAPITAL LETTER A WITH DIAERESIS
	{ 0x5C,    0, 0x000000D6 }, // LATIN CAPITAL LETTER O WITH DIAERESIS
	{ 0x5D,    0, 0x000000D1 }, // LATIN CAPITAL LETTER N WITH TILDE
	{ 0x5E,    0, 0x000000DC }, // LATIN CAPITAL LETTER U WITH DIAERESIS
	{ 0x5F,    0, 0x000000A7 }, // SECTION SIGN
	{ 0x60,    0, 0x000000BF }, // INVERTED QUESTION MARK
	{ 0x61,    0, 0x00000061 }, // LATIN SMALL LETTER A
	{ 0x62,    0, 0x00000062 }, // LATIN SMALL LETTER B
	{ 0x63,    0, 0x00000063 }, // LATIN SMALL LETTER C
	{ 0x64,    0, 0x00000064 }, // LATIN SMALL LETTER D
	{ 0x65,    0, 0x00000065 }, // LATIN SMALL LETTER E
	{ 0x66,    0, 0x00000066 }, // LATIN SMALL LETTER F
	{ 0x67,    0, 0x00000067 }, // LATIN SMALL LETTER G
	{ 0x68,    0, 0x00000068 }, // LATIN SMALL LETTER H
	{ 0x69,    0, 0x00000069 }, // LATIN SMALL LETTER I
	{ 0x6A,    0, 0x0000006A }, // LATIN SMALL LETTER J
	{ 0x6B,    0, 0x0000006B }, // LATIN SMALL LETTER K
	{ 0x6C,    0, 0x0000006C }, // LATIN SMALL LETTER L
	{ 0x6D,    0, 0x0000006D }, // LATIN SMALL LETTER M
	{ 0x6E,    0, 0x0000006E }, // LATIN SMALL LETTER N
	{ 0x6F,    0, 0x0000006F }, // LATIN SMALL LETTER O
	{ 0x70,    0, 0x00000070 }, // LATIN SMALL LETTER P
	{ 0x71,    0, 0x00000071 }, // LATIN SMALL LETTER Q
	{ 0x72,    0, 0x00000072 }, // LATIN SMALL LETTER R
	{ 0x73,    0, 0x00000073 }, // LATIN SMALL LETTER S
	{ 0x74,    0, 0x00000074 }, // LATIN SMALL LETTER T
	{ 0x75,    0, 0x00000075 }, // LATIN SMALL LETTER U
	{ 0x76,    0, 0x00000076 }, // LATIN SMALL LETTER V
	{ 0x77,    0, 0x00000077 }, // LATIN SMALL LETTER W
	{ 0x78,    0, 0x00000078 }, // LATIN SMALL LETTER X
	{ 0x79,    0, 0x00000079 }, // LATIN SMALL LETTER Y
	{ 0x7A,    0, 0x0000007A }, // LATIN SMALL LETTER Z
	{ 0x7B,    0, 0x000000E4 }, // LATIN SMALL LETTER A WITH DIAERESIS
	{ 0x7C,    0, 0x000000F6 }, // LATIN SMALL LETTER O WITH DIAERESIS
	{ 0x7D,    0, 0x000000F1 }, // LATIN SMALL LETTER N WITH TILDE
	{ 0x7E,    0, 0x000000FC }, // LATIN SMALL LETTER U WITH DIAERESIS
	{ 0x7F,    0, 0x000000E0 }, // LATIN SMALL LETTER A WITH GRAVE
};

wchar_t gsm_to_wc(char gsm)
{
	int i;
	for (i=0; i<ARRAY_SIZE(vgsm_7bit_wc_translations); i++) {
		if (vgsm_7bit_wc_translations[i].c == gsm)
			return vgsm_7bit_wc_translations[i].wc;
	}

	return L'\0';
}

int wc_to_gsm(wchar_t wc, __u8 *c, __u8 *c2)
{
	int i;
	for (i=0; i<ARRAY_SIZE(vgsm_7bit_wc_translations); i++) {
		if (vgsm_7bit_wc_translations[i].wc == wc) {
			*c = vgsm_7bit_wc_translations[i].c;

			if (vgsm_7bit_wc_translations[i].c2) {
				*c2 = vgsm_7bit_wc_translations[i].c2;
				return 2;
			}

			return 1;
		}
	}

	return 0;
}

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

		out[i] = gsm_to_wc((val & mask) >> shift);
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
		int cnt = wc_to_gsm(inwc, &c, &c2);

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

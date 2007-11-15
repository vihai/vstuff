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

const char *get_token(const char **s, char *token, int token_size)
{
	const char *p = *s;
	const char *old_s = *s;
	char *token_p = token;

	for(;;) {
		if (*p == '"') {
			p++;

			while(*p && *p != '"') {
				*token_p++ = *p++;

				if (token_p == token + token_size - 2)
					break;
			}

			if (*p == '"')
				p++;
		}

		if (!*p) {
			*s = p;
			break;
		}

		if (*p == ',') {
			*s = p + 1;

			break;
		}

		*token_p++ = *p++;

		if (token_p == token + token_size - 2)
			break;
	}

	if (*s == old_s)
		return NULL;

	*token_p = '\0';

	return token;
}


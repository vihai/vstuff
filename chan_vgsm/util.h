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

#ifndef _VGSM_UTIL_H
#define _VGSM_UTIL_H

#include <wchar.h>

#include <linux/types.h>

#include <asterisk/logger.h>
#include <asterisk/version.h>

extern struct vgsm_state vgsm;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

typedef unsigned char BOOL;

#ifdef DEBUG_CODE
#define vgsm_debug_serial(format, arg...)		\
	if (vgsm.debug_serial)				\
		ast_verbose("vgsm: "			\
			format,				\
			## arg)
#define vgsm_debug_generic(format, arg...)		\
	if (vgsm.debug_generic)				\
		ast_verbose("vgsm: "			\
			format,				\
			## arg)
#define vgsm_debug_serial_verb(format, arg...)		\
	if (vgsm.debug_serial)				\
		ast_verbose(VERBOSE_PREFIX_1		\
			format,				\
			## arg)
#define vgsm_debug_generic_verb(format, arg...)		\
	if (vgsm.debug_generic)				\
		ast_verbose(VERBOSE_PREFIX_1		\
			format,				\
			## arg)
#else
#define vgsm_debug_serial(format, arg...)		\
	do {} while(0);
#define vgsm_debug_generic(format, arg...)		\
	do {} while(0);
#define vgsm_debug_serial_verb(format, arg...)		\
	do {} while(0);
#define vgsm_debug_generic_verb(format, arg...)		\
	do {} while(0);
#endif

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

#define min(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

#if 0
#define ast_mutex_lock(a) \
	do {				\
		ast_verbose("LOCK " # a "  %s:%d\n", __FILE__, __LINE__); \
		ast_mutex_lock(a);	\
	} while(0)

#define ast_mutex_unlock(a) \
	do {				\
		ast_verbose("UNLOCK " # a "  %s:%d\n", __FILE__, __LINE__); \
		ast_mutex_unlock(a);	\
	} while(0)
#endif

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#define ast_localtime(time, tm, tz) localtime_r(time, tm)
#endif

int sanprintf(char *buf, int bufsize, const char *fmt, ...);
wchar_t *w_unprintable_remove(wchar_t *dst, const wchar_t *src, int dst_size);
char *unprintable_escape(const char *str, char *buf, int bufsize);

int char_to_hexdigit(char c);

const char *get_token(const char **s, char *token, int token_size);

#endif

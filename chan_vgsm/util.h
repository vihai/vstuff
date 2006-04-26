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

#ifndef _VGSM_UTIL_H
#define _VGSM_UTIL_H

#include <wchar.h>

#include <linux/types.h>

extern struct vgsm_state vgsm;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE !FALSE
#endif

typedef char BOOL;

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

typedef long long longtime_t;

longtime_t longtime_now();
int sanprintf(char *buf, int bufsize, const char *fmt, ...);
wchar_t *w_unprintable_remove(wchar_t *dst, const wchar_t *src, int dst_size);
char *unprintable_escape(const char *str, char *buf, int bufsize);

__u8 swap_nibbles(__u8 val);
__u8 nibbles_to_decimal(__u8 val);
__u8 decimal_to_nibbles(__u8 val);

int char_to_hexdigit(char c);
char vgsm_bcd_to_char(__u8 bcd);
int vgsm_bcd_to_text(
	__u8 *buf, int nibbles,
	char *str, int str_len);
int vgsm_text_to_bcd(__u8 *buf, char *str);
wchar_t gsm_to_wc(char gsm);
int wc_to_gsm(wchar_t wc, __u8 *c, __u8 *c1);
void vgsm_7bit_to_wc(const __u8 *buf, int septets, wchar_t *out, int outsize);
int vgsm_wc_to_7bit(const wchar_t *buf, int buf_len, __u8 *out);

#endif

/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _UTIL_H
#define _UTIL_H

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#ifdef DEBUG_CODE
#define visdn_debug(format, arg...)			\
	if (visdn.debug)				\
		ast_verbose(VERBOSE_PREFIX_3		\
			format,				\
			## arg)

#define FUNC_DEBUG(format, arg...)	\
	visdn_debug("%s " format "\n", __FUNCTION__, ## arg);

#else
#define visdn_debug(format, arg...)		\
	do {} while(0);
#define FUNC_DEBUG() do {} while(0)
#endif

#endif

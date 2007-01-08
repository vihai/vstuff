/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef __KSTREAMER_PRIV_H
#define __KSTREAMER_PRIV_H

#define ks_MODULE_NAME "kstreamer"
#define ks_MODULE_PREFIX ks_MODULE_NAME ": "
#define ks_MODULE_DESCR "kstreamer"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define ks_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG ks_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define ks_debug(format, arg...) do {} while (0)
#endif

#define ks_msg(level, format, arg...)			\
	printk(level ks_MODULE_PREFIX			\
		format,						\
		## arg)

extern int debug_level;

#endif

/*
 * VoiSmart vDSP board driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_H
#define _VGSM_H


#ifdef __KERNEL__

#define vdsp_DRIVER_NAME "vdsp"
#define vdsp_DRIVER_PREFIX vdsp_DRIVER_NAME ": "
#define vdsp_DRIVER_DESCR "VoiSmart vDSP Driver"

#define vdsp_msg(level, format, arg...) \
	printk(level vdsp_DRIVER_PREFIX format, ## arg)

#ifdef DEBUG_CODE
extern int debug_level;

#define vdsp_debug(dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG vdsp_DRIVER_PREFIX format, ## arg)
#define vdsp_debug_cont(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(format, ## arg)
#else
#define vdsp_debug(dbglevel, format, arg...) do {} while (0)
#define vdsp_debug_cont(dbglevel, format, arg...) do {} while (0)
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE !FALSE
#endif

#ifndef BOOL
#define BOOL char
#endif

#endif

#endif

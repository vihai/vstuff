/*
 * Cologne Chip's HFC-USB vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_USB_H
#define _HFC_USB_H

#include <linux/delay.h>
#include <linux/usb.h>

#include "regs.h"

#ifdef DEBUG
#define hfc_debug(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)		\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX format, ## arg)

#else
#define hfc_debug(dbglevel, format, arg...) do {} while (0)
#endif

#define hfc_msg(level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		format, ## arg)

#define hfc_DRIVER_NAME "hfc-usb"
#define hfc_DRIVER_PREFIX hfc_DRIVER_NAME ": "
#define hfc_DRIVER_DESCR "HFC-USB Driver"

#ifndef intptr_t
#define intptr_t unsigned long
#endif

#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef BOOL
#define BOOL unsigned char
#endif

enum hfc_direction { RX = 0, TX = 1 };

extern int debug_level;

#endif

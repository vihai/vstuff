/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM2_H
#define _VGSM2_H

#define VGSM_IOC_GET_INTERFACE_VERSION	_IOR(0xd1, 0, unsigned int)
#define VGSM_IOC_GET_NODEID		_IOR(0xd1, 1, unsigned int)
#define VGSM_IOC_POWER_GET		_IOR(0xd1, 3, unsigned int)
#define VGSM_IOC_POWER_IGN		_IOR(0xd1, 4, unsigned int)
#define VGSM_IOC_POWER_EMERG_OFF	_IOR(0xd1, 5, unsigned int)
#define VGSM_IOC_FW_VERSION		_IOR(0xd1, 7, unsigned int)
#define VGSM_IOC_FW_UPGRADE		_IOR(0xd1, 8, unsigned int)
#define VGSM_IOC_SIM_ROUTE		_IOR(0xd1, 10, unsigned int)
#define VGSM_IOC_SIM_GET_ID		_IOR(0xd1, 16, unsigned int)
#define VGSM_IOC_SIM_GET_CLOCK		_IOR(0xd1, 11, unsigned int)
#define VGSM_IOC_SIM_SET_CLOCK		_IOR(0xd1, 12, unsigned int)
#define VGSM_IOC_FW_READ		_IOR(0xd1, 13, unsigned int)
#define VGSM_IOC_IDENTIFY		_IOR(0xd1, 14, unsigned int)
#define VGSM_IOC_READ_SERIAL		_IOR(0xd1, 15, unsigned int)

struct vgsm2_fw_header
{
	int size;
	int checksum;
	unsigned char data[0];
};

#ifdef __KERNEL__

#include <linux/version.h>

#define vgsm_DRIVER_NAME "vgsm2"
#define vgsm_DRIVER_PREFIX vgsm_DRIVER_NAME ": "
#define vgsm_DRIVER_DESCR "VoiSmart vGSM-II Driver"

#define vgsm_msg(level, format, arg...) \
	printk(level vgsm_DRIVER_PREFIX format, ## arg)

#ifdef DEBUG_CODE
extern int debug_level;

#define vgsm_debug(dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG vgsm_DRIVER_PREFIX format, ## arg)
#define vgsm_debug_cont(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(format, ## arg)
#else
#define vgsm_debug(dbglevel, format, arg...) do {} while (0)
#define vgsm_debug_cont(dbglevel, format, arg...) do {} while (0)
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

extern struct ks_dynattr *vgsm_amu_compander_class;
extern struct ks_dynattr *vgsm_amu_decompander_class;

void vgsm_led_update(void);

#endif

#endif

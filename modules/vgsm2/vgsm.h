/*
 * VoiSmart vGSM-II board driver
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

#define VGSM_IOC_GET_CHANID		_IOR(0xd1, 0, unsigned int)
#define VGSM_IOC_CODEC_SET		_IOR(0xd1, 1, unsigned int)
#define VGSM_IOC_POWER_GET		_IOR(0xd1, 2, unsigned int)
#define VGSM_IOC_POWER_IGN		_IOR(0xd1, 3, unsigned int)
#define VGSM_IOC_POWER_EMERG_OFF	_IOR(0xd1, 4, unsigned int)
#define VGSM_IOC_PAD_TIMEOUT		_IOR(0xd1, 5, unsigned int)
#define VGSM_IOC_FW_VERSION		_IOR(0xd1, 6, unsigned int)
#define VGSM_IOC_FW_UPGRADE		_IOR(0xd1, 7, unsigned int)
#define VGSM_IOC_GET_TX_FIFOLEN		_IOR(0xd1, 8, unsigned int)

struct vgsm_fw_header
{
	int size;
	int checksum;
	unsigned char data[0];
};

#ifdef __KERNEL__

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

extern struct list_head vgsm_cards_list;
extern spinlock_t vgsm_cards_list_lock;

extern struct tty_driver *vgsm_tty_driver;

#endif

#endif

/*
 * VoiSmart vGSM-I card driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_H
#define _VGSM_H

#define VGSM_IOC_GET_INTERFACE_VERSION	_IOR(0xd1, 0, unsigned int)
#define VGSM_IOC_GET_NODEID		_IOR(0xd1, 1, unsigned int)
#define VGSM_IOC_CODEC_SET		_IOR(0xd1, 2, unsigned int)
#define VGSM_IOC_POWER_GET		_IOR(0xd1, 3, unsigned int)
#define VGSM_IOC_POWER_IGN		_IOR(0xd1, 4, unsigned int)
#define VGSM_IOC_POWER_EMERG_OFF	_IOR(0xd1, 5, unsigned int)
#define VGSM_IOC_PAD_TIMEOUT		_IOR(0xd1, 6, unsigned int)
#define VGSM_IOC_FW_VERSION		_IOR(0xd1, 7, unsigned int)
#define VGSM_IOC_FW_UPGRADE		_IOR(0xd1, 8, unsigned int)
#define VGSM_IOC_GET_TX_FIFOLEN		_IOR(0xd1, 9, unsigned int)
//#define VGSM_IOC_SET_SIM_ROUTE	_IOR(0xd1, 10, unsigned int)
//#define VGSM_IOC_SIM_GET_CLOCK	_IOR(0xd1, 11, unsigned int)
//#define VGSM_IOC_SIM_SET_CLOCK	_IOR(0xd1, 12, unsigned int)
//#define VGSM_IOC_FW_READ		_IOR(0xd1, 13, unsigned int)
//#define VGSM_IOC_IDENTIFY		_IOR(0xd1, 14, unsigned int)
//#define VGSM_IOC_READ_SERIAL		_IOR(0xd1, 15, unsigned int)
//#define VGSM_IOC_SIM_GET_ID		_IOR(0xd1, 16, unsigned int)
//#define VGSM_IOC_FW_FLASH_VERSION	_IOR(0xd1, 17, unsigned int)
//#define VGSM_IOC_FW_UPGRADE_STAT	_IOR(0xd1, 18, unsigned int)
//#define VGSM_IOC_GET_SIM_ROUTE	_IOR(0xd1, 19, unsigned int)
//#define VGSM_IOC_SIM_GET_CARD_ID	_IOR(0xd1, 20, unsigned int)
//#define VGSM_IOC_CARD_GET_ID		_IOR(0xd1, 21, unsigned int)

struct vgsm_fw_header
{
	int size;
	int checksum;
	unsigned char data[0];
};

struct vgsm_codec_ctl
{
	int parameter;
	int value;
};

struct vgsm_ctl
{
	char node_id[80];
};

enum vgsm_codec_parameter
{
	VGSM_CODEC_RESET,
	VGSM_CODEC_RXGAIN,
	VGSM_CODEC_TXGAIN,
	VGSM_CODEC_DIG_LOOP,
	VGSM_CODEC_ANAL_LOOP,
};

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/spinlock.h>

#define vgsm_DRIVER_NAME "vgsm"
#define vgsm_DRIVER_PREFIX vgsm_DRIVER_NAME ": "
#define vgsm_DRIVER_DESCR "VoiSmart vGSM-I card driver"

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
unsigned long wait_for_completion_timeout(
	struct completion *x, unsigned long timeout);
#endif

#endif

#endif

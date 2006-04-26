/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
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

struct vgsm_codec_ctl
{
	int parameter;
	int value;
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

#define vgsm_DRIVER_NAME "vgsm-pci"
#define vgsm_DRIVER_PREFIX vgsm_DRIVER_NAME ": "
#define vgsm_DRIVER_DESCR "VoiSmart PCI GSM Wildcard Driver"

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

extern dev_t vgsm_first_dev;
extern struct class vgsm_class;

#endif

#endif

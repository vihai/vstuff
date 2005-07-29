#ifndef _HFC_UTIL_H
#define _HFC_UTIL_H

#ifdef DEBUG
#define hfc_debug(dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX format, ## arg)
#define hfc_debug_cont(dbglevel, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(format, ## arg)
#else
#define hfc_debug(dbglevel, format, arg...) do {} while (0)
#define hfc_debug_cont(dbglevel, format, arg...) do {} while (0)
#endif


#define hfc_msg(level, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		format, ## arg)

#ifndef intptr_t
#define intptr_t unsigned long
#endif

#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

#ifndef PCI_DMA_32BIT
#define PCI_DMA_32BIT	0x00000000ffffffffULL
#endif

#ifndef PCI_DEVICE_ID_CCD_08B4
#define PCI_DEVICE_ID_CCD_08B4		0x08b4
#endif

#ifndef PCI_DEVICE_ID_CCD_16B8
#define PCI_DEVICE_ID_CCD_16B8		0x16B8
#endif

#define hfc_PCI_MEM_SIZE	0x1000

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

extern int debug_level;

enum hfc_direction { RX = 0, TX = 1 };

#endif

/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_REGS_H
#define _VGSM_REGS_H

/* Tiger 320 registers */
#define VGSM_CNTL    		0x00
#define VGSM_CNTL_EXTRST		(1 << 0)
#define VGSM_CNTL_DMA_RST		(1 << 1)
#define VGSM_CNTL_SERIAL_RST		(1 << 2)
#define VGSM_CNTL_PIB_CYCLE_3		(0 << 4)
#define VGSM_CNTL_PIB_CYCLE_8		(1 << 4)
#define VGSM_CNTL_PIB_CYCLE_15		(3 << 4)
#define VGSM_CNTL_DMA_INT_PERSISTENT	(1 << 6)
#define VGSM_CNTL_DMA_SELF		(0 << 7)
#define VGSM_CNTL_DMA_NORMAL		(1 << 7)

#define VGSM_DMA_OPER			0x01
#define VGSM_DMA_OPER_DMA_ENABLE	(1 << 0)
#define VGSM_DMA_OPER_DMA_RESTART	(1 << 1)

#define VGSM_AUXC    		0x02
#define VGSM_AUXC_0_IN		(0 << 0)
#define VGSM_AUXC_0_OUT		(1 << 0)
#define VGSM_AUXC_1_IN		(0 << 1)
#define VGSM_AUXC_1_OUT		(1 << 1)
#define VGSM_AUXC_2_IN		(0 << 2)
#define VGSM_AUXC_2_OUT		(1 << 2)
#define VGSM_AUXC_3_IN		(0 << 3)
#define VGSM_AUXC_3_OUT		(1 << 3)
#define VGSM_AUXC_4_IN		(0 << 4)
#define VGSM_AUXC_4_OUT		(1 << 4)
#define VGSM_AUXC_5_IN		(0 << 5)
#define VGSM_AUXC_5_OUT		(1 << 5)
#define VGSM_AUXC_6_IN		(0 << 6)
#define VGSM_AUXC_6_OUT		(1 << 6)
#define VGSM_AUXC_7_IN		(0 << 7)
#define VGSM_AUXC_7_OUT		(1 << 7)

#define VGSM_AUXD    		0x03

#define VGSM_MASK0   		0x04
#define VGSM_MASK0_DMA_WR_INT		(1 << 0)
#define VGSM_MASK0_DMA_WR_END		(1 << 1)
#define VGSM_MASK0_DMA_RD_INT		(1 << 2)
#define VGSM_MASK0_DMA_RD_END		(1 << 3)
#define VGSM_MASK0_PCI_MASTER_ABORT	(1 << 4)
#define VGSM_MASK0_PCI_TARGET_ABORT	(1 << 5)

#define VGSM_MASK1   		0x05
#define VGSM_INT0STAT 		0x06

#define VGSM_INT1STAT		0x07
#define VGSM_INT1STAT_WR_REACH_INT	(1 << 0)
#define VGSM_INT1STAT_WR_REACH_END	(1 << 1)
#define VGSM_INT1STAT_RD_REACH_INT	(1 << 2)
#define VGSM_INT1STAT_RD_REACH_END	(1 << 3)
#define VGSM_INT1STAT_PCI_MASTER_ABORT	(1 << 4)
#define VGSM_INT1STAT_PCI_TARGET_ABORT	(1 << 5)

/* DMA registers */
#define VGSM_DMA_WR_START	0x08
#define VGSM_DMA_WR_INT		0x0C
#define VGSM_DMA_WR_END		0x10
#define VGSM_DMA_WR_CUR		0x14
#define VGSM_DMA_RD_START	0x18
#define VGSM_DMA_RD_INT		0x1C
#define VGSM_DMA_RD_END		0x20
#define VGSM_DMA_RD_CUR		0x24


/* AUX control registers */
#define VGSM_AUXFUNC		0x2B
#define VGSM_SERCTL			0x2D
#define VGSM_SERCTL_2XDCLK			(1 << 0)
#define VGSM_SERCTL_DCLK_10NS			(0 << 1)
#define VGSM_SERCTL_DCLK_20NS			(1 << 1)
#define VGSM_SERCTL_DCLK_30NS			(2 << 1)
#define VGSM_SERCTL_DCLK_40NS			(3 << 1)
#define VGSM_SERCTL_INVERT_DCLK			(1 << 3)
#define VGSM_SERCTL_SHIFT_IN_LSB_FIRST	(0 << 6)
#define VGSM_SERCTL_SHIFT_IN_MSB_FIRST	(1 << 6)
#define VGSM_SERCTL_SHIFT_OUT_LSB_FIRST	(0 << 7)
#define VGSM_SERCTL_SHIFT_OUT_MSB_FIRST	(1 << 7)

#define VGSM_AUX_POL		0x2A
#define VGSM_FSCDELAY		0x2F


/* PIB address space */
#define VGSM_PIB_C0		0xC0
#define VGSM_PIB_C4		0xC4
#define VGSM_PIB_C8		0xC8
#define VGSM_PIB_CC		0xCC
#define VGSM_PIB_D0		0xD0
#define VGSM_PIB_D4		0xD4
#define VGSM_PIB_D8		0xD8
#define VGSM_PIB_DC		0xDC
#define VGSM_PIB_E0		0xE0
#define VGSM_PIB_E4		0xE4
#define VGSM_PIB_E8		0xE8
#define VGSM_PIB_EC		0xEC
#define VGSM_PIB_F0		0xF0
#define VGSM_PIB_F4		0xF4
#define VGSM_PIB_F8		0xF8
#define VGSM_PIB_FC		0xFC	

#define VGSM_CMD_S0		0x0
#define VGSM_CMD_S1		0x1

#define VGSM_CMD_MAINT		0x2
#define VGSM_CMD_MAINT_ACK		0
#define VGSM_CMD_MAINT_ONOFF		1
#define VGSM_CMD_MAINT_ONOFF_EMERG_OFF		(1 << 0)
#define VGSM_CMD_MAINT_ONOFF_IGN		(1 << 1)

#define VGSM_CMD_MAINT_GET_FW_VER	2
#define VGSM_CMD_MAINT_TIMEOUT_SET	3
#define VGSM_CMD_MAINT_CODEC_SET	4
#define VGSM_CMD_MAINT_CODEC_GET	5
#define VGSM_CMD_MAINT_POWER_GET	6

#define VGSM_CMD_FW_UPGRADE	0x3

#endif

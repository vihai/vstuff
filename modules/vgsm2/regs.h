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

#ifndef _VGSM_REGS_H
#define _VGSM_REGS_H

#define VGSM_R_INT_STATUS 0x0000
#define VGSM_R_INT_STATUS_V_ME(n)		(1 << (16 + (n)))
#define VGSM_R_INT_STATUS_V_SIM(n)		(1 << (24 + (n)))

#define VGSM_R_INT_ENABLE 0x0004
#define VGSM_R_INT_ENABLE_V_ME(n)		(1 << (16 + (n)))
#define VGSM_R_INT_ENABLE_V_SIM(n)		(1 << (24 + (n)))

#define VGSM_R_SIM_ROUTER 0x0010
#define VGSM_R_SIM_ROUTER_V_ME_SOURCE(me,sim)	((sim) << ((me)*4))
#define VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(me)	(0xf << ((me)*4))

#define VGSM_R_LED_SRC 0x0030
#define VGSM_R_LED_SRC_V_REAR_0_R0		(1 << 0)
#define VGSM_R_LED_SRC_V_REAR_0_G0		(1 << 1)
#define VGSM_R_LED_SRC_V_REAR_0_R1		(1 << 2)
#define VGSM_R_LED_SRC_V_REAR_0_G1		(1 << 3)
#define VGSM_R_LED_SRC_V_REAR_1_R0		(1 << 4)
#define VGSM_R_LED_SRC_V_REAR_1_G0		(1 << 5)
#define VGSM_R_LED_SRC_V_REAR_1_R1		(1 << 6)
#define VGSM_R_LED_SRC_V_REAR_1_G1		(1 << 7)
#define VGSM_R_LED_SRC_V_REAR_2_R0		(1 << 8)
#define VGSM_R_LED_SRC_V_REAR_2_G0		(1 << 9)
#define VGSM_R_LED_SRC_V_REAR_2_R1		(1 << 10)
#define VGSM_R_LED_SRC_V_REAR_2_G1		(1 << 11)
#define VGSM_R_LED_SRC_V_REAR_3_R0		(1 << 12)
#define VGSM_R_LED_SRC_V_REAR_3_G0		(1 << 13)
#define VGSM_R_LED_SRC_V_REAR_3_R1		(1 << 14)
#define VGSM_R_LED_SRC_V_REAR_3_G1		(1 << 15)
#define VGSM_R_LED_SRC_V_TOP_0_R		(1 << 16)
#define VGSM_R_LED_SRC_V_TOP_0_G		(1 << 17)
#define VGSM_R_LED_SRC_V_TOP_1_R		(1 << 18)
#define VGSM_R_LED_SRC_V_TOP_1_G		(1 << 19)
#define VGSM_R_LED_SRC_V_TOP_2_R		(1 << 20)
#define VGSM_R_LED_SRC_V_TOP_2_G		(1 << 21)
#define VGSM_R_LED_SRC_V_TOP_3_R		(1 << 22)
#define VGSM_R_LED_SRC_V_TOP_3_G		(1 << 23)
#define VGSM_R_LED_SRC_V_STATUS_G		(1 << 24)
#define VGSM_R_LED_SRC_V_STATUS_R		(1 << 25)

#define VGSM_R_LED_HARD 0x0034
#define VGSM_R_LED_HARD_V_REAR_0_R0		(1 << 0)
#define VGSM_R_LED_HARD_V_REAR_0_G0		(1 << 1)
#define VGSM_R_LED_HARD_V_REAR_0_R1		(1 << 2)
#define VGSM_R_LED_HARD_V_REAR_0_G1		(1 << 3)
#define VGSM_R_LED_HARD_V_REAR_1_R0		(1 << 4)
#define VGSM_R_LED_HARD_V_REAR_1_G0		(1 << 5)
#define VGSM_R_LED_HARD_V_REAR_1_R1		(1 << 6)
#define VGSM_R_LED_HARD_V_REAR_1_G1		(1 << 7)
#define VGSM_R_LED_HARD_V_REAR_2_R0		(1 << 8)
#define VGSM_R_LED_HARD_V_REAR_2_G0		(1 << 9)
#define VGSM_R_LED_HARD_V_REAR_2_R1		(1 << 10)
#define VGSM_R_LED_HARD_V_REAR_2_G1		(1 << 11)
#define VGSM_R_LED_HARD_V_REAR_3_R0		(1 << 12)
#define VGSM_R_LED_HARD_V_REAR_3_G0		(1 << 13)
#define VGSM_R_LED_HARD_V_REAR_3_R1		(1 << 14)
#define VGSM_R_LED_HARD_V_REAR_3_G1		(1 << 15)
#define VGSM_R_LED_HARD_V_TOP_0_R		(1 << 16)
#define VGSM_R_LED_HARD_V_TOP_0_G		(1 << 17)
#define VGSM_R_LED_HARD_V_TOP_1_R		(1 << 18)
#define VGSM_R_LED_HARD_V_TOP_1_G		(1 << 19)
#define VGSM_R_LED_HARD_V_TOP_2_R		(1 << 20)
#define VGSM_R_LED_HARD_V_TOP_2_G		(1 << 21)
#define VGSM_R_LED_HARD_V_TOP_3_R		(1 << 22)
#define VGSM_R_LED_HARD_V_TOP_3_G		(1 << 23)
#define VGSM_R_LED_HARD_V_STATUS_G		(1 << 24)
#define VGSM_R_LED_HARD_V_STATUS_R		(1 << 25)

#define VGSM_R_LED_USER 0x0038
#define VGSM_R_LED_USER_V_REAR_0_R0		(1 << 0)
#define VGSM_R_LED_USER_V_REAR_0_G0		(1 << 1)
#define VGSM_R_LED_USER_V_REAR_0_R1		(1 << 2)
#define VGSM_R_LED_USER_V_REAR_0_G1		(1 << 3)
#define VGSM_R_LED_USER_V_REAR_1_R0		(1 << 4)
#define VGSM_R_LED_USER_V_REAR_1_G0		(1 << 5)
#define VGSM_R_LED_USER_V_REAR_1_R1		(1 << 6)
#define VGSM_R_LED_USER_V_REAR_1_G1		(1 << 7)
#define VGSM_R_LED_USER_V_REAR_2_R0		(1 << 8)
#define VGSM_R_LED_USER_V_REAR_2_G0		(1 << 9)
#define VGSM_R_LED_USER_V_REAR_2_R1		(1 << 10)
#define VGSM_R_LED_USER_V_REAR_2_G1		(1 << 11)
#define VGSM_R_LED_USER_V_REAR_3_R0		(1 << 12)
#define VGSM_R_LED_USER_V_REAR_3_G0		(1 << 13)
#define VGSM_R_LED_USER_V_REAR_3_R1		(1 << 14)
#define VGSM_R_LED_USER_V_REAR_3_G1		(1 << 15)
#define VGSM_R_LED_USER_V_TOP_0_R		(1 << 16)
#define VGSM_R_LED_USER_V_TOP_0_G		(1 << 17)
#define VGSM_R_LED_USER_V_TOP_1_R		(1 << 18)
#define VGSM_R_LED_USER_V_TOP_1_G		(1 << 19)
#define VGSM_R_LED_USER_V_TOP_2_R		(1 << 20)
#define VGSM_R_LED_USER_V_TOP_2_G		(1 << 21)
#define VGSM_R_LED_USER_V_TOP_3_R		(1 << 22)
#define VGSM_R_LED_USER_V_TOP_3_G		(1 << 23)
#define VGSM_R_LED_USER_V_STATUS_G		(1 << 24)
#define VGSM_R_LED_USER_V_STATUS_R		(1 << 25)

#define VGSM_R_STATUS 0x0080
#define VGSM_R_STATUS_V_BUSY			(1 << 0)
#define VGSM_R_STATUS_V_PLL_LOCKED		(1 << 1)

#define VGSM_R_INFO 0x0084
#define VGSM_R_INFO_V_ME_CNT(n)			((n) & 0x0f >> 0)
#define VGSM_R_INFO_V_SIM_CNT(n)		((n) & 0xf0 >> 4)

#define VGSM_R_VERSION 0x0088

#define VGSM_R_SERVICE 0x0090
#define VGSM_R_SERVICE_V_RESET			(1 << 0)
#define VGSM_R_SERVICE_V_RECONFIG		(1 << 1)
#define VGSM_R_SERVICE_V_BIG_ENDIAN		(1 << 2)

#define VGSM_R_TEST 0x0094

#define VGSM_R_ASMI_CTL 0x0100
#define VGSM_R_ASMI_CTL_V_DATAIN(v)		((v) << 0)
#define VGSM_R_ASMI_CTL_V_START			(1 << 16)
#define VGSM_R_ASMI_CTL_V_RDEN			(1 << 17)
#define VGSM_R_ASMI_CTL_V_READ			(1 << 18)
#define VGSM_R_ASMI_CTL_V_WREN			(1 << 19)
#define VGSM_R_ASMI_CTL_V_WRITE			(1 << 20)
#define VGSM_R_ASMI_CTL_V_READ_SID		(1 << 21)
#define VGSM_R_ASMI_CTL_V_READ_STATUS		(1 << 22)
#define VGSM_R_ASMI_CTL_V_SECTOR_PROTECT	(1 << 23)
#define VGSM_R_ASMI_CTL_V_SECTOR_ERASE		(1 << 24)
#define VGSM_R_ASMI_CTL_V_BULK_ERASE		(1 << 25)


#define VGSM_R_ASMI_STA 0x0100
#define VGSM_R_ASMI_STA_V_DATAOUT(v)		((v) & 0xff)
#define VGSM_R_ASMI_STA_V_EPCS_ID(v)		(((v) & 0xff00) >> 8)
#define VGSM_R_ASMI_STA_V_STATUS(v)		(((v) & 0xff0000) >> 16)
#define VGSM_R_ASMI_STA_V_RUNNING		(1 << 24)
#define VGSM_R_ASMI_STA_V_BUSY			(1 << 25)
#define VGSM_R_ASMI_STA_V_DATA_VALID		(1 << 26)
#define VGSM_R_ASMI_STA_V_ILLEGAL_ERASE		(1 << 27)
#define VGSM_R_ASMI_STA_V_ILLEGAL_WRITE		(1 << 28)

#define VGSM_R_ASMI_ADDR 0x0104
#define VGSM_R_ASMI_IO 0x0108

/* SIM controllers */
#define VGSM_SIMS_BASE 0x1000
#define VGSM_SIM_SPACE 0x0100
#define VGSM_SIM_BASE(n) (VGSM_SIMS_BASE + (n) * VGSM_SIM_SPACE)

#define VGSM_R_SIM_SETUP(n) (VGSM_SIM_BASE(n) + 0x0000)
#define VGSM_R_SIM_SETUP_V_VCC		(1 << 0)
#define VGSM_R_SIM_SETUP_V_3V		(1 << 1)
#define VGSM_R_SIM_SETUP_V_CLOCK(n)	((n) << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_ME	(0x0 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_3_5	(0x1 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_4	(0x2 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_5	(0x3 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_7_1	(0x4 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_8	(0x5 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_10	(0x6 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_14	(0x7 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_16	(0x8 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_20	(0x9 << 4)
#define VGSM_R_SIM_SETUP_V_CLOCK_OFF	(0xf << 4)

#define VGSM_R_SIM_STATUS(n) (VGSM_SIM_BASE(n) + 0x0004)
#define VGSM_R_SIM_STATUS_V_CCIN	(1 << 0)

#define VGSM_R_SIM_INT_STATUS(n) (VGSM_SIM_BASE(n) + 0x0008)
#define VGSM_R_SIM_INT_STATUS_V_CCIN		(1 << 0)
#define VGSM_R_SIM_INT_STATUS_V_UART		(1 << 31)

#define VGSM_R_SIM_INT_ENABLE(n) (VGSM_SIM_BASE(n) + 0x000c)
#define VGSM_R_SIM_INT_ENABLE_V_CCIN	(1 << 0)
#define VGSM_R_SIM_INT_ENABLE_V_UART	(1 << 31)

#define VGSM_SIM_UART_BASE(n) (VGSM_SIM_BASE(n) + 0x0040)

/* ME controllers */
#define VGSM_MES_BASE 0x4000
#define VGSM_ME_SPACE 0x0800
#define VGSM_ME_BASE(n) (VGSM_MES_BASE + (n) * VGSM_ME_SPACE)

#define VGSM_R_ME_SETUP(n) (VGSM_ME_BASE(n) + 0x0000)
#define VGSM_R_ME_SETUP_V_ON		(1 << 0)
#define VGSM_R_ME_SETUP_V_EMERG_OFF	(1 << 1)

#define VGSM_R_ME_STATUS(n) (VGSM_ME_BASE(n) + 0x0004)
#define VGSM_R_ME_STATUS_V_VDD		(1 << 0)
#define VGSM_R_ME_STATUS_V_VDDLP	(1 << 1)
#define VGSM_R_ME_STATUS_V_CCVCC	(1 << 2)

#define VGSM_R_ME_INT_STATUS(n) (VGSM_ME_BASE(n) + 0x0008)
#define VGSM_R_ME_INT_STATUS_V_VDD		(1 << 0)
#define VGSM_R_ME_INT_STATUS_V_VDDLP		(1 << 1)
#define VGSM_R_ME_INT_STATUS_V_CCVCC		(1 << 2)
#define VGSM_R_ME_INT_STATUS_V_DAI_RX_INT	(1 << 15)
#define VGSM_R_ME_INT_STATUS_V_DAI_RX_END	(1 << 16)
#define VGSM_R_ME_INT_STATUS_V_DAI_TX_INT	(1 << 17)
#define VGSM_R_ME_INT_STATUS_V_DAI_TX_END	(1 << 18)
#define VGSM_R_ME_INT_STATUS_V_UART_ASC0	(1 << 24)
#define VGSM_R_ME_INT_STATUS_V_UART_ASC1	(1 << 25)
#define VGSM_R_ME_INT_STATUS_V_UART_MESIM	(1 << 26)

#define VGSM_R_ME_INT_ENABLE(n) (VGSM_ME_BASE(n) + 0x000c)
#define VGSM_R_ME_INT_ENABLE_V_VDD		(1 << 0)
#define VGSM_R_ME_INT_ENABLE_V_VDDLP		(1 << 1)
#define VGSM_R_ME_INT_ENABLE_V_CCVCC		(1 << 2)
#define VGSM_R_ME_INT_ENABLE_V_DAI_RX_INT	(1 << 16)
#define VGSM_R_ME_INT_ENABLE_V_DAI_RX_END	(1 << 17)
#define VGSM_R_ME_INT_ENABLE_V_DAI_TX_INT	(1 << 18)
#define VGSM_R_ME_INT_ENABLE_V_DAI_TX_END	(1 << 19)
#define VGSM_R_ME_INT_ENABLE_V_UART_ASC0	(1 << 24)
#define VGSM_R_ME_INT_ENABLE_V_UART_ASC1	(1 << 25)
#define VGSM_R_ME_INT_ENABLE_V_UART_MESIM	(1 << 26)

#define VGSM_R_ME_FIFO_SIZE(n) (VGSM_ME_BASE(n) + 0x0010)
#define VGSM_R_ME_FIFO_SIZE_V_RX_SIZE(v)	((v) & 0x0000ffff)
#define VGSM_R_ME_FIFO_SIZE_V_TX_SIZE(v)	(((v) & 0xffff0000) >> 16)

#define VGSM_R_ME_FIFO_SETUP(n) (VGSM_ME_BASE(n) + 0x0014)
#define VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR	(0x0 << 0)
#define VGSM_R_ME_FIFO_SETUP_V_RX_ALAW		(0x1 << 0)
#define VGSM_R_ME_FIFO_SETUP_V_RX_MULAW		(0x2 << 0)
#define VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR	(0x0 << 16)
#define VGSM_R_ME_FIFO_SETUP_V_TX_ALAW		(0x1 << 16)
#define VGSM_R_ME_FIFO_SETUP_V_TX_MULAW		(0x2 << 16)

#define VGSM_R_ME_FIFO_RX_IN(n)		(VGSM_ME_BASE(n) + 0x0020)
#define VGSM_R_ME_FIFO_RX_OUT(n)	(VGSM_ME_BASE(n) + 0x0024)
#define VGSM_R_ME_FIFO_RX_INT(n)	(VGSM_ME_BASE(n) + 0x0028)
#define VGSM_R_ME_FIFO_TX_IN(n)		(VGSM_ME_BASE(n) + 0x0030)
#define VGSM_R_ME_FIFO_TX_OUT(n)	(VGSM_ME_BASE(n) + 0x0034)
#define VGSM_R_ME_FIFO_TX_INT(n)	(VGSM_ME_BASE(n) + 0x0038)

#define VGSM_ME_ASC0_BASE(n)		(VGSM_ME_BASE(n) + 0x0100)
#define VGSM_ME_ASC1_BASE(n)		(VGSM_ME_BASE(n) + 0x0140)
#define VGSM_ME_SIM_BASE(n)		(VGSM_ME_BASE(n) + 0x0180)

#define VGSM_FIFO_BASE 0x00000000
#define VGSM_FIFO_SPACE 0x2000
#define VGSM_FIFO_RX_BASE(me)	(VGSM_FIFO_BASE + ((me) * VGSM_FIFO_SPACE) + 0x0000)
#define VGSM_FIFO_TX_BASE(me)	(VGSM_FIFO_BASE + ((me) * VGSM_FIFO_SPACE) + 0x1000)

#endif

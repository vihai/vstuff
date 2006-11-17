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

#ifndef _VGSM_REGS_H
#define _VGSM_REGS_H

#define VGSM_FIFO_TX_0_BASE	0x0000
#define VGSM_FIFO_TX_0_SIZE	0x0400
#define VGSM_FIFO_TX_0_OUT	0x2000
#define VGSM_FIFO_TX_0_INT	0x2010

#define VGSM_FIFO_TX_1_BASE	0x0400
#define VGSM_FIFO_TX_1_SIZE	0x0400
#define VGSM_FIFO_TX_1_OUT	0x2004
#define VGSM_FIFO_TX_1_INT	0x2014

#define VGSM_FIFO_TX_2_BASE	0x0800
#define VGSM_FIFO_TX_2_SIZE	0x0400
#define VGSM_FIFO_TX_2_OUT	0x2008
#define VGSM_FIFO_TX_2_INT	0x2018

#define VGSM_FIFO_TX_3_BASE	0x0c00
#define VGSM_FIFO_TX_3_SIZE	0x0400
#define VGSM_FIFO_TX_3_OUT	0x200c
#define VGSM_FIFO_TX_3_INT	0x201c

#define VGSM_FIFO_RX_0_BASE	0x1000
#define VGSM_FIFO_RX_0_SIZE	0x0400
#define VGSM_FIFO_RX_0_OUT	0x2020
#define VGSM_FIFO_RX_0_INT	0x2030
#define VGSM_FIFO_RX_0_IN	0x2040

#define VGSM_FIFO_RX_1_BASE	0x1400
#define VGSM_FIFO_RX_1_SIZE	0x0400
#define VGSM_FIFO_RX_1_OUT	0x2024
#define VGSM_FIFO_RX_1_INT	0x2034
#define VGSM_FIFO_RX_1_IN	0x2044

#define VGSM_FIFO_RX_2_BASE	0x1800
#define VGSM_FIFO_RX_2_SIZE	0x0400
#define VGSM_FIFO_RX_2_OUT	0x2028
#define VGSM_FIFO_RX_2_INT	0x2038
#define VGSM_FIFO_RX_2_IN	0x2048

#define VGSM_FIFO_RX_3_BASE	0x1c00
#define VGSM_FIFO_RX_3_SIZE	0x0400
#define VGSM_FIFO_RX_3_OUT	0x202c
#define VGSM_FIFO_RX_3_INT	0x203c
#define VGSM_FIFO_RX_3_IN	0x204c

#define VGSM_UART_SIM_0_BASE	0x4000
#define VGSM_UART_SIM_1_BASE	0x4010
#define VGSM_UART_SIM_2_BASE	0x4020
#define VGSM_UART_SIM_3_BASE	0x4030
#define VGSM_UART_ASC0_0_BASE	0x4040
#define VGSM_UART_ASC0_1_BASE	0x4050
#define VGSM_UART_ASC0_2_BASE	0x4060
#define VGSM_UART_ASC0_3_BASE	0x4070
#define VGSM_UART_ASC1_0_BASE	0x4080
#define VGSM_UART_ASC1_1_BASE	0x4090
#define VGSM_UART_ASC1_2_BASE	0x40a0
#define VGSM_UART_ASC1_3_BASE	0x40b0
#define VGSM_UART_MESIM_0_BASE	0x40c0
#define VGSM_UART_MESIM_1_BASE	0x40d0
#define VGSM_UART_MESIM_2_BASE	0x40e0
#define VGSM_UART_MESIM_3_BASE	0x40f0

#define VGSM_R_INT_STATUS 0x6000
#define VGSM_R_INT_STATUS_V_UART_INT		(1 << 0)
#define VGSM_R_INT_STATUS_V_ME_STATUS_INT	(1 << 1)
#define VGSM_R_INT_STATUS_V_RX_DAI_END_0	(1 << 16)
#define VGSM_R_INT_STATUS_V_RX_DAI_INT_0	(1 << 17)
#define VGSM_R_INT_STATUS_V_RX_DAI_END_1	(1 << 18)
#define VGSM_R_INT_STATUS_V_RX_DAI_INT_1	(1 << 19)
#define VGSM_R_INT_STATUS_V_RX_DAI_END_2	(1 << 20)
#define VGSM_R_INT_STATUS_V_RX_DAI_INT_2	(1 << 21)
#define VGSM_R_INT_STATUS_V_RX_DAI_END_3	(1 << 22)
#define VGSM_R_INT_STATUS_V_RX_DAI_INT_3	(1 << 23)
#define VGSM_R_INT_STATUS_V_TX_DAI_END_0	(1 << 24)
#define VGSM_R_INT_STATUS_V_TX_DAI_INT_0	(1 << 25)
#define VGSM_R_INT_STATUS_V_TX_DAI_END_1	(1 << 26)
#define VGSM_R_INT_STATUS_V_TX_DAI_INT_1	(1 << 27)
#define VGSM_R_INT_STATUS_V_TX_DAI_END_2	(1 << 28)
#define VGSM_R_INT_STATUS_V_TX_DAI_INT_2	(1 << 29)
#define VGSM_R_INT_STATUS_V_TX_DAI_END_3	(1 << 30)
#define VGSM_R_INT_STATUS_V_TX_DAI_INT_3	(1 << 31)

#define VGSM_R_INT_ENABLE 0x6004
#define VGSM_R_INT_ENABLE_V_UART_INT		(1 << 0)
#define VGSM_R_INT_ENABLE_V_ME_STATUS_INT	(1 << 1)
#define VGSM_R_INT_ENABLE_V_RX_DAI_END_0	(1 << 16)
#define VGSM_R_INT_ENABLE_V_RX_DAI_INT_0	(1 << 17)
#define VGSM_R_INT_ENABLE_V_RX_DAI_END_1	(1 << 18)
#define VGSM_R_INT_ENABLE_V_RX_DAI_INT_1	(1 << 19)
#define VGSM_R_INT_ENABLE_V_RX_DAI_END_2	(1 << 20)
#define VGSM_R_INT_ENABLE_V_RX_DAI_INT_2	(1 << 21)
#define VGSM_R_INT_ENABLE_V_RX_DAI_END_3	(1 << 22)
#define VGSM_R_INT_ENABLE_V_RX_DAI_INT_3	(1 << 23)
#define VGSM_R_INT_ENABLE_V_TX_DAI_END_0	(1 << 24)
#define VGSM_R_INT_ENABLE_V_TX_DAI_INT_0	(1 << 25)
#define VGSM_R_INT_ENABLE_V_TX_DAI_END_1	(1 << 26)
#define VGSM_R_INT_ENABLE_V_TX_DAI_INT_1	(1 << 27)
#define VGSM_R_INT_ENABLE_V_TX_DAI_END_2	(1 << 28)
#define VGSM_R_INT_ENABLE_V_TX_DAI_INT_2	(1 << 29)
#define VGSM_R_INT_ENABLE_V_TX_DAI_END_3	(1 << 30)
#define VGSM_R_INT_ENABLE_V_TX_DAI_INT_3	(1 << 31)

#define VGSM_R_UART_INT_STATUS 0x6008
#define VGSM_R_UART_INT_STATUS_V_SIM_0		(1 << 0)
#define VGSM_R_UART_INT_STATUS_V_SIM_1		(1 << 1)
#define VGSM_R_UART_INT_STATUS_V_SIM_2		(1 << 2)
#define VGSM_R_UART_INT_STATUS_V_SIM_3		(1 << 3)
#define VGSM_R_UART_INT_STATUS_V_ASC0_0	(1 << 4)
#define VGSM_R_UART_INT_STATUS_V_ASC0_1	(1 << 5)
#define VGSM_R_UART_INT_STATUS_V_ASC0_2	(1 << 6)
#define VGSM_R_UART_INT_STATUS_V_ASC0_3	(1 << 7)
#define VGSM_R_UART_INT_STATUS_V_ASC1_0	(1 << 8)
#define VGSM_R_UART_INT_STATUS_V_ASC1_1	(1 << 9)
#define VGSM_R_UART_INT_STATUS_V_ASC1_2	(1 << 10)
#define VGSM_R_UART_INT_STATUS_V_ASC1_3	(1 << 11)
#define VGSM_R_UART_INT_STATUS_V_SIM_ME_0	(1 << 12)
#define VGSM_R_UART_INT_STATUS_V_SIM_ME_1	(1 << 13)
#define VGSM_R_UART_INT_STATUS_V_SIM_ME_2	(1 << 14)
#define VGSM_R_UART_INT_STATUS_V_SIM_ME_3	(1 << 15)

#define VGSM_R_SIM_SETUP 0x600c
#define VGSM_R_SIM_SETUP_V_ON_SIM_0	(1 << 0)
#define VGSM_R_SIM_SETUP_V_ON_SIM_1	(1 << 1)
#define VGSM_R_SIM_SETUP_V_ON_SIM_2	(1 << 2)
#define VGSM_R_SIM_SETUP_V_ON_SIM_3	(1 << 3)
#define VGSM_R_SIM_SETUP_V_VCC_SIM_0	(1 << 4)
#define VGSM_R_SIM_SETUP_V_VCC_SIM_1	(1 << 5)
#define VGSM_R_SIM_SETUP_V_VCC_SIM_2	(1 << 6)
#define VGSM_R_SIM_SETUP_V_VCC_SIM_3	(1 << 7)

#define VGSM_R_SIM_STATUS 0x600c
#define VGSM_R_SIM_STATUS_V_PLL_LOCKED	(1 << 0)

#define VGSM_R_ME_SETUP 0x6010
#define VGSM_R_ME_SETUP_V_ON_ME_0		(1 << 0)
#define VGSM_R_ME_SETUP_V_ON_ME_1		(1 << 1)
#define VGSM_R_ME_SETUP_V_ON_ME_2		(1 << 2)
#define VGSM_R_ME_SETUP_V_ON_ME_3		(1 << 3)
#define VGSM_R_ME_SETUP_V_EMERG_OFF_ME_0	(1 << 4)
#define VGSM_R_ME_SETUP_V_EMERG_OFF_ME_1	(1 << 5)
#define VGSM_R_ME_SETUP_V_EMERG_OFF_ME_2	(1 << 6)
#define VGSM_R_ME_SETUP_V_EMERG_OFF_ME_3	(1 << 7)
#define VGSM_R_ME_SETUP_V_PASSTHRU_0		(1 << 8)
#define VGSM_R_ME_SETUP_V_PASSTHRU_1		(1 << 9)
#define VGSM_R_ME_SETUP_V_PASSTHRU_2		(1 << 10)
#define VGSM_R_ME_SETUP_V_PASSTHRU_3		(1 << 11)

#define VGSM_R_ME_STATUS 0x6010
#define VGSM_R_ME_STATUS_EN_ME_0		(1 << 0)
#define VGSM_R_ME_STATUS_EN_ME_1		(1 << 1)
#define VGSM_R_ME_STATUS_EN_ME_2		(1 << 2)
#define VGSM_R_ME_STATUS_EN_ME_3		(1 << 3)
#define VGSM_R_ME_STATUS_CCVCC_ME_0		(1 << 4)
#define VGSM_R_ME_STATUS_CCVCC_ME_1		(1 << 5)
#define VGSM_R_ME_STATUS_CCVCC_ME_2		(1 << 6)
#define VGSM_R_ME_STATUS_CCVCC_ME_3		(1 << 7)

#define VGSM_R_LED 0x6014
#define VGSM_R_LED_V_FRONT_0_R0			(1 << 0)
#define VGSM_R_LED_V_FRONT_0_G0			(1 << 1)
#define VGSM_R_LED_V_FRONT_0_R1			(1 << 2)
#define VGSM_R_LED_V_FRONT_0_G1			(1 << 3)
#define VGSM_R_LED_V_FRONT_1_R0			(1 << 4)
#define VGSM_R_LED_V_FRONT_1_G0			(1 << 5)
#define VGSM_R_LED_V_FRONT_1_R1			(1 << 6)
#define VGSM_R_LED_V_FRONT_1_G1			(1 << 7)
#define VGSM_R_LED_V_FRONT_2_R0			(1 << 8)
#define VGSM_R_LED_V_FRONT_2_G0			(1 << 9)
#define VGSM_R_LED_V_FRONT_2_R1			(1 << 10)
#define VGSM_R_LED_V_FRONT_2_G1			(1 << 11)
#define VGSM_R_LED_V_FRONT_3_R0			(1 << 12)
#define VGSM_R_LED_V_FRONT_3_G0			(1 << 13)
#define VGSM_R_LED_V_FRONT_3_R1			(1 << 14)
#define VGSM_R_LED_V_FRONT_3_G1			(1 << 15)
#define VGSM_R_LED_V_SIM_0_R			(1 << 16)
#define VGSM_R_LED_V_SIM_0_G			(1 << 17)
#define VGSM_R_LED_V_SIM_1_R			(1 << 18)
#define VGSM_R_LED_V_SIM_1_G			(1 << 19)
#define VGSM_R_LED_V_SIM_2_R			(1 << 20)
#define VGSM_R_LED_V_SIM_2_G			(1 << 21)
#define VGSM_R_LED_V_SIM_3_R			(1 << 22)
#define VGSM_R_LED_V_SIM_3_G			(1 << 23)
#define VGSM_R_LED_V_USER_G			(1 << 24)
#define VGSM_R_LED_V_SYNC_OVERRIDE		(1 << 28)

#define VGSM_R_INFO 0x6018
#define VGSM_R_INFO_V_VERSION(v)		(((v) & 0xf) >> 0)

#define VGSM_R_TEST 0x601c

#define VGSM_ASMI		0x8000
#define VGSM_PCI_CONTROL	0xC000

#endif

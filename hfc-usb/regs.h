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

#ifndef _HFC_REGS_H
#define _HFC_REGS_H

#define HFC_REG_CIRM    		0x00
#define HFC_REG_CIRM_2_CLKI		(0x0 << 0)
#define HFC_REG_CIRM_6_CLKI		(0x1 << 0)
#define HFC_REG_CIRM_10_CLKI		(0x2 << 0)
#define HFC_REG_CIRM_14_CLKI		(0x3 << 0)
#define HFC_REG_CIRM_18_CLKI		(0x4 << 0)
#define HFC_REG_CIRM_22_CLKI		(0x5 << 0)
#define HFC_REG_CIRM_26_CLKI		(0x6 << 0)
#define HFC_REG_CIRM_30_CLKI		(0x7 << 0)
#define HFC_REG_CIRM_RESET		(1 << 3)
#define HFC_REG_CIRM_AUX_TRISTATE	(0 << 4)
#define HFC_REG_CIRM_AUX_FIXED		(1 << 4)

// int length register
#define HFC_REG_USB_SIZE		0x07
#define HFC_REG_USB_SIZE_OUT(val)	(((val)/8) << 0)
#define HFC_REG_USB_SIZE_IN(val)	(((val)/8) << 4)

// iso length register
#define HFC_REG_USB_SIZE_I		0x06
#define HFC_REG_USB_SIZE_I_VAL(val)	((val) << 0)

#define HFC_REG_F_CROSS			0x0b
#define HFC_REG_F_CROSS_REV_B1_TX	(1 << 0)
#define HFC_REG_F_CROSS_REV_B1_RX	(1 << 1)
#define HFC_REG_F_CROSS_REV_B2_TX	(1 << 2)
#define HFC_REG_F_CROSS_REV_B2_RX	(1 << 3)
#define HFC_REG_F_CROSS_REV_D_TX	(1 << 4)
#define HFC_REG_F_CROSS_REV_D_RX	(1 << 5)
#define HFC_REG_F_CROSS_REV_PCM_TX	(1 << 6)
#define HFC_REG_F_CROSS_REV_PCM_RX	(1 << 7)

#define HFC_REG_F_MODE			0x0d
#define HFC_REG_F_MODE_CSM		(1 << 7)

#define HFC_REG_INC_RES_F		0x0e
#define HFC_REG_INC_RES_F_INC		(1 << 0)
#define HFC_REG_INC_RES_F_RESET		(1 << 1)

#define HFC_REG_FIFO			0x0f
#define HFC_REG_FIFO_B1_TX		(0x0 << 0)
#define HFC_REG_FIFO_B1_RX		(0x1 << 0)
#define HFC_REG_FIFO_B2_TX		(0x2 << 0)
#define HFC_REG_FIFO_B2_RX		(0x3 << 0)
#define HFC_REG_FIFO_D_TX		(0x4 << 0)
#define HFC_REG_FIFO_D_RX		(0x5 << 0)
#define HFC_REG_FIFO_PCM_TX		(0x6 << 0)
#define HFC_REG_FIFO_PCM_RX		(0x7 << 0)

#define HFC_REG_FIF_DATA		0x80
#define HFC_REG_FIF_DATA_NOINC		0x84
#define HFC_REG_F_USAGE			0x1a

#define HFC_REG_FIF_F1			0x0c
#define HFC_REG_FIF_F2			0x0d
#define HFC_REG_FIF_Z1			0x04
#define HFC_REG_FIF_Z2			0x06

#define HFC_REG_F_THRESH		0x0c
#define HFC_REG_F_THRESH_TX(val)	((val) << 0)
#define HFC_REG_F_THRESH_RX(val)	((val) << 4)


#define HFC_REG_INT_S1			0x10
#define HFC_REG_INT_S1_B1_TX		(1 << 0)
#define HFC_REG_INT_S1_B1_RX		(1 << 1)
#define HFC_REG_INT_S1_B2_TX		(1 << 2)
#define HFC_REG_INT_S1_B2_RX		(1 << 3)
#define HFC_REG_INT_S1_D_TX		(1 << 4)
#define HFC_REG_INT_S1_D_RX		(1 << 5)
#define HFC_REG_INT_S1_PCM_TX		(1 << 6)
#define HFC_REG_INT_S1_PCM_RX		(1 << 7)

#define HFC_REG_INT_S2			0x11
#define HFC_REG_INT_S2_STATE		(1 << 0)
#define HFC_REG_INT_S2_TIMER		(1 << 1)
#define HFC_REG_INT_S2_PROC		(1 << 2)
#define HFC_REG_INT_S2_GCI		(1 << 3)
#define HFC_REG_INT_S2_RR		(1 << 4)
#define HFC_REG_INT_S2_USB		(1 << 5)

#define HFC_REG_INT_M1			0x1a
#define HFC_REG_INT_M1_B1_TX		(1 << 0)
#define HFC_REG_INT_M1_B1_RX		(1 << 1)
#define HFC_REG_INT_M1_B2_TX		(1 << 2)
#define HFC_REG_INT_M1_B2_RX		(1 << 3)
#define HFC_REG_INT_M1_D_TX		(1 << 4)
#define HFC_REG_INT_M1_D_RX		(1 << 5)
#define HFC_REG_INT_M1_PCM_TX		(1 << 6)
#define HFC_REG_INT_M1_PCM_RX		(1 << 7)

#define HFC_REG_INT_M2			0x1b
#define HFC_REG_INT_M2_STATE		(1 << 0)
#define HFC_REG_INT_M2_TIMER		(1 << 1)
#define HFC_REG_INT_M2_PROC		(1 << 2)
#define HFC_REG_INT_M2_GCI		(1 << 3)
#define HFC_REG_INT_M2_RR		(1 << 4)
#define HFC_REG_INT_M2_USB		(1 << 5)
#define HFC_REG_INT_M2_INT_REV		(1 << 6)
#define HFC_REG_MINT_2_INT_ENA		(1 << 7)

#define HFC_REG_HDLC_PAR		0xfb
#define HFC_REG_HDLC_PAR_PROC_8BITS	(0x0 << 0)
#define HFC_REG_HDLC_PAR_PROC_1BITS	(0x1 << 0)
#define HFC_REG_HDLC_PAR_PROC_2BITS	(0x2 << 0)
#define HFC_REG_HDLC_PAR_PROC_3BITS	(0x3 << 0)
#define HFC_REG_HDLC_PAR_PROC_4BITS	(0x4 << 0)
#define HFC_REG_HDLC_PAR_PROC_5BITS	(0x5 << 0)
#define HFC_REG_HDLC_PAR_PROC_6BITS	(0x6 << 0)
#define HFC_REG_HDLC_PAR_PROC_7BITS	(0x7 << 0)
#define HFC_REG_HDLC_PAR_START_0BIT	(0x0 << 3)
#define HFC_REG_HDLC_PAR_START_1BIT	(0x1 << 3)
#define HFC_REG_HDLC_PAR_START_2BIT	(0x2 << 3)
#define HFC_REG_HDLC_PAR_START_3BIT	(0x3 << 3)
#define HFC_REG_HDLC_PAR_START_4BIT	(0x4 << 3)
#define HFC_REG_HDLC_PAR_START_5BIT	(0x5 << 3)
#define HFC_REG_HDLC_PAR_START_6BIT	(0x6 << 3)
#define HFC_REG_HDLC_PAR_START_7BIT	(0x7 << 3)
#define HFC_REG_HDLC_PAR_LOOP		(1 << 6)
#define HFC_REG_HDLC_PAR_INVERT		(1 << 7)

// channel connect register
#define HFC_REG_CON_HDLC		0xfa
#define HFC_REG_CON_HDLC_IFF		(1 << 0)
#define HFC_REG_CON_HDLC_HDLC		(0 << 1)
#define HFC_REG_CON_HDLC_HDLC_ENA	(0x2 << 2)
#define HFC_REG_CON_HDLC_TRANS		(1 << 1)
#define HFC_REG_CON_HDLC_TRANS_8	(0x0 << 2)
#define HFC_REG_CON_HDLC_TRANS_16	(0x1 << 2)
#define HFC_REG_CON_HDLC_TRANS_32	(0x2 << 2)
#define HFC_REG_CON_HDLC_TRANS_64	(0x3 << 2)
#define HFC_REG_CON_FIFO_from_ST	(0 << 5)
#define HFC_REG_CON_FIFO_from_PCM	(1 << 5)
#define HFC_REG_CON_ST_from_FIFO	(0 << 6)
#define HFC_REG_CON_ST_from_PCM		(1 << 6)
#define HFC_REG_CON_PCM_from_FIFO	(0 << 7)
#define HFC_REG_CON_PCM_from_ST		(1 << 7)

// ID value of HFC-USB
#define HFC_REG_CHIP_ID			0x16

#define HFC_REG_STATUS			0x1c
#define HFC_REG_STATUS_BUSY		(1 << 0)
#define HFC_REG_STATUS_PROCESSING	(1 << 1)
#define HFC_REG_STATUS_INT		(1 << 6)
#define HFC_REG_STATUS_FRAME_INT	(1 << 7)

#define HFC_REG_P_ADR_W			0x13
#define HFC_REG_P_DATA			0x1f

#define HFC_REG_B1_SSL			0x20
#define HFC_REG_B2_SSL			0x21
#define HFC_REG_AUX1_SSL		0x22
#define HFC_REG_AUX2_SSL		0x23
#define HFC_REG_B1_RSL			0x24
#define HFC_REG_B2_RSL			0x25
#define HFC_REG_AUX1_RSL		0x26
#define HFC_REG_AUX2_RSL		0x27

#define HFC_REG_MST_MODE0		0x14
#define HFC_REG_MST_MODE0_SLAVE		(0 << 0)
#define HFC_REG_MST_MODE0_MASTER	(1 << 0)
#define HFC_REG_MST_MODE0_C4_NEGATIVE	(0 << 1)
#define HFC_REG_MST_MODE0_C4_POSITIVE	(1 << 1)
#define HFC_REG_MST_MODE0_F0_NEGATIVE	(0 << 2)
#define HFC_REG_MST_MODE0_F0_POSITIVE	(1 << 2)
#define HFC_REG_MST_MODE0_F0_1_C4	(0 << 3)
#define HFC_REG_MST_MODE0_F0_2_C4	(1 << 3)
#define HFC_REG_MST_MODE0_CODECA_F1A_B1	(0x0 << 4)
#define HFC_REG_MST_MODE0_CODECA_F1A_B2	(0x1 << 4)
#define HFC_REG_MST_MODE0_CODECA_F1A_AUX1	(0x2 << 4)
#define HFC_REG_MST_MODE0_CODECA_F1A_C2O	(0x3 << 4)
#define HFC_REG_MST_MODE0_CODECB_F1B_B1	(0x0 << 6)
#define HFC_REG_MST_MODE0_CODECB_F1B_B2	(0x1 << 6)
#define HFC_REG_MST_MODE0_CODECB_F1B_AUX1	(0x2 << 6)
#define HFC_REG_MST_MODE0_CODECB_F1B_AUX2	(0x3 << 6)

#define HFC_REG_MST_MODE1		0x15
#define HFC_REG_MST_MODE1_AUX1_MIRROR	(0 << 0)
#define HFC_REG_MST_MODE1_AUX2_MIRROR	(0 << 1)
#define HFC_REG_MST_MODE1_DPLL_4	(0x0 << 2)
#define HFC_REG_MST_MODE1_DPLL_2	(0x1 << 2)
#define HFC_REG_MST_MODE1_DPLL_3	(0x2 << 2)
#define HFC_REG_MST_MODE1_DPLL_1	(0x3 << 2)
#define HFC_REG_MST_MODE1_PCM30		(0x0 << 4)
#define HFC_REG_MST_MODE1_PCM64		(0x1 << 4)
#define HFC_REG_MST_MODE1_PCM128	(0x2 << 4)
#define HFC_REG_MST_MODE1_TEST_LOOP	(1 << 6)
#define HFC_REG_MST_MODE1_PCM_WRITE	(1 << 7)

#define HFC_REG_MST_MODE2		0x16
#define HFC_REG_MST_MODE2_F1A_OKI	(1 << 0)
#define HFC_REG_MST_MODE2_F1B_OKI	(1 << 1)
#define HFC_REG_MST_MODE2_PCM_31_0	(0x0 << 4)
#define HFC_REG_MST_MODE2_PCM_63_32	(0x1 << 4)
#define HFC_REG_MST_MODE2_PCM_95_64	(0x2 << 4)
#define HFC_REG_MST_MODE2_PCM_127_96	(0x3 << 4)

#define HFC_REG_F0_CNT_L		0x17
#define HFC_REG_F0_CNT_L_VAL(val)	((val) & 0x00ff)
#define HFC_REG_F0_CNT_H		0x18
#define HFC_REG_F0_CNT_H_VAL(val)	(((val) & 0xff00) >> 8)

#define HFC_REG_C_I			0x28

#define HFC_REG_TRxR			0x29
#define HFC_REG_MON1_D			0x2A
#define HFC_REG_MON2_D			0x2B

#define HFC_REG_STATES			0x30
#define HFC_REG_STATES_STATE_VAL(val)	((val) << 0)
#define HFC_REG_STATES_FRAME_SYNC	(1 << 4)
#define HFC_REG_STATES_T2_EXPIRED	(1 << 5)
#define HFC_REG_STATES_INFO0		(1 << 6)
#define HFC_REG_STATES_LOAD_STATE	(1 << 4)
#define HFC_REG_STATES_START_DEACTIVATION	(0x2 << 5)
#define HFC_REG_STATES_START_ACTIVATION		(0x3 << 5)
#define HFC_REG_STATES_G2_G3		(1 << 7)

// S-bus control register (tx)
#define HFC_REG_SCTRL			0x31
#define HFC_REG_SCTRL_B1_ENA		(1 << 0)
#define HFC_REG_SCTRL_B2_ENA		(1 << 1)
#define HFC_REG_SCTRL_TE		(0 << 2)
#define HFC_REG_SCTRL_NT		(1 << 2)
#define HFC_REG_SCTRL_D_HIPRI		(0 << 3)
#define HFC_REG_SCTRL_D_LOPRI		(1 << 3)
#define HFC_REG_SCTRL_SQ_ENA		(1 << 4)
#define HFC_REG_SCTRL_96_KHZ		(1 << 5)
#define HFC_REG_SCTRL_CAPACITIVE	(0 << 6)
#define HFC_REG_SCTRL_NON_CAPACITIVE	(1 << 6)
#define HFC_REG_SCTRL_POWER_DOWN	(1 << 7)

// same for E and special funcs
#define HFC_REG_SCTRL_E			0x32
#define HFC_REG_SCTRL_E_FORCE_G2_G3	(1 << 0)
#define HFC_REG_SCTRL_E_D_RESET		(1 << 2)
#define HFC_REG_SCTRL_E_D_U_ENABLE	(1 << 3)
#define HFC_REG_SCTRL_E_E_ZERO		(1 << 4)
#define HFC_REG_SCTRL_E_SWAP_B1_B2	(1 << 7)

// S-bus control register (rx)
#define HFC_REG_SCTRL_R			0x33
#define HFC_REG_SCTRL_R_B1_ENA		(1 << 0)
#define HFC_REG_SCTRL_R_B2_ENA		(1 << 1)

#define HFC_REG_SQ_REC			0x34
#define HFC_REG_SQ_SEND			0x34

// bit delay register
#define HFC_REG_CLKDEL			0x37

#define HFC_B1_REC			0x3c
#define HFC_B1_SEND			0x3c
#define HFC_B2_REC			0x3d
#define HFC_B2_SEND			0x3d
#define HFC_D_REC			0x3e
#define HFC_D_SEND			0x3e
#define HFC_E_REC			0x3f

#endif

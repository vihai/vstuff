/*
 * Cologne Chip's HFC-S PCI A vISDN driver
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

#define hfc_RESET_DELAY		20
#define hfc_FIFO_SIZE		0x8000
#define hfc_PCI_MEM_SIZE	0x0100


#define hfc_PCI_MWBA		0x80

/* GCI/IOM bus monitor registers */

#define hfc_C_I			0x08
#define hfc_TRxR		0x0C
#define hfc_MON1_D		0x28
#define hfc_MON2_D		0x2C


/* GCI/IOM bus timeslot registers */

#define hfc_B1_SSL		0x80
#define hfc_B2_SSL		0x84
#define hfc_AUX1_SSL		0x88
#define hfc_AUX2_SSL		0x8C
#define hfc_B1_RSL		0x90
#define hfc_B2_RSL		0x94
#define hfc_AUX1_RSL		0x98
#define hfc_AUX2_RSL		0x9C

/* GCI/IOM bus data registers */

#define hfc_B1_D		0xA0
#define hfc_B2_D		0xA4
#define hfc_AUX1_D		0xA8
#define hfc_AUX2_D		0xAC

/* GCI/IOM bus configuration registers */

#define hfc_MST_EMOD		0xB4
#define	hfc_MST_EMOD_D_MASK		(0x7 << 3)
#define	hfc_MST_EMOD_D_HFC_from_ST	(0 << 3)
#define	hfc_MST_EMOD_D_HFC_from_GCI	(1 << 3)
#define hfc_MST_EMOD_D_ST_from_HFC	(0 << 4)
#define hfc_MST_EMOD_D_ST_from_GCI	(1 << 4)
#define hfc_MST_EMOD_D_GCI_from_HFC	(0 << 5)
#define hfc_MST_EMOD_GD_CI_from_ST	(1 << 5)

#define hfc_MST_MODE		0xB8
#define hfc_MST_MODE_MASTER		(1 << 0)
#define hfc_MST_MODE_SLAVE		0x00

#define hfc_CONNECT		0xBC
#define hfc_CONNECT_B1_MASK		(0x7 << 0)
#define	hfc_CONNECT_B1_HFC_from_ST	(0 << 0)
#define	hfc_CONNECT_B1_HFC_from_GCI	(1 << 0)
#define hfc_CONNECT_B1_ST_from_HFC	(0 << 1)
#define hfc_CONNECT_B1_ST_from_GCI	(1 << 1)
#define hfc_CONNECT_B1_GCI_from_HFC	(0 << 2)
#define hfc_CONNECT_B1_GCI_from_ST	(1 << 2)

#define hfc_CONNECT_B2_MASK		(0x7 << 3)
#define	hfc_CONNECT_B2_HFC_from_ST	(0 << 3)
#define	hfc_CONNECT_B2_HFC_from_GCI	(1 << 3)
#define hfc_CONNECT_B2_ST_from_HFC	(0 << 4)
#define hfc_CONNECT_B2_ST_from_GCI	(1 << 4)
#define hfc_CONNECT_B2_GCI_from_HFC	(0 << 5)
#define hfc_CONNECT_B2_GCI_from_ST	(1 << 5)


/* Interrupt and status registers */

#define hfc_FIFO_EN		0x44
#define hfc_FIFO_EN_B1TX		(1 << 0)
#define hfc_FIFO_EN_B1RX		(1 << 1)
#define hfc_FIFO_EN_B2TX		(1 << 2)
#define hfc_FIFO_EN_B2RX		(1 << 3)
#define hfc_FIFO_EN_DTX			(1 << 4)
#define hfc_FIFO_EN_DRX			(1 << 5)
#define hfc_FIFO_EN_B1			(hfc_FIFO_EN_B1TX|hfc_FIFO_EN_B1RX)
#define hfc_FIFO_EN_B2			(hfc_FIFO_EN_B2TX|hfc_FIFO_EN_B2RX)
#define hfc_FIFO_EN_D			(hfc_FIFO_EN_DTX|hfc_FIFO_EN_DRX)

#define hfc_TRM			0x48
#define hfc_TRM_TRANS_INT_00		(0x0 << 0)
#define hfc_TRM_TRANS_INT_01		(0x1 << 0)
#define hfc_TRM_TRANS_INT_10		(0x2 << 0)
#define hfc_TRM_TRANS_INT_11		(0x3 << 0)
#define hfc_TRM_ECHO			(1 << 5)
#define hfc_TRM_B1_PLUS_B2		(1 << 6)
#define hfc_TRM_IOM_TEST_LOOP		(1 << 7)

#define hfc_B_MODE		0x4C
#define hfc_CHIP_ID		0x58

#define hfc_CIRM		0x60
#define hfc_CIRM_AUX_MSK		0x07
#define hfc_CIRM_RESET			(1 << 3)
#define hfc_CIRM_B1_REV			(1 << 6)
#define hfc_CIRM_B2_REV			(1 << 7)

#define hfc_CTMT		0x64
#define hfc_CTMT_TRANSB1		(1 << 0)
#define hfc_CTMT_TRANSB2		(1 << 1)
#define hfc_CTMT_TIMER_CLEAR		(1 << 7)
#define hfc_CTMT_TIMER_MASK		0x1C
#define hfc_CTMT_TIMER_3_125		(0x01 << 2)
#define hfc_CTMT_TIMER_6_25		(0x02 << 2)
#define hfc_CTMT_TIMER_12_5		(0x03 << 2)
#define hfc_CTMT_TIMER_25		(0x04 << 2)
#define hfc_CTMT_TIMER_50		(0x05 << 2)
#define hfc_CTMT_TIMER_400		(0x06 << 2)
#define hfc_CTMT_TIMER_800		(0x07 << 2)
#define hfc_CTMT_AUTO_TIMER		(1 << 5)

#define hfc_INT_M1		0x68
#define hfc_INT_M1_B1TRANS		(1 << 0)
#define hfc_INT_M1_B2TRANS		(1 << 1)
#define hfc_INT_M1_DTRANS		(1 << 2)
#define hfc_INT_M1_B1REC		(1 << 3)
#define hfc_INT_M1_B2REC		(1 << 4)
#define hfc_INT_M1_DREC			(1 << 5)
#define hfc_INT_M1_L1STATE		(1 << 6)
#define hfc_INT_M1_TIMER		(1 << 7)

#define hfc_INT_M2		0x6C
#define hfc_INT_M2_PROC_TRANS		(1 << 0)
#define hfc_INT_M2_GCI_I_CHG		(1 << 1)
#define hfc_INT_M2_GCI_MON_REC		(1 << 2)
#define hfc_INT_M2_IRQ_ENABLE		(1 << 3)
#define hfc_INT_M2_PMESEL		(1 << 7)

#define hfc_INT_S1		0x78
#define hfc_INT_S1_B1TRANS		(1 << 0)
#define hfc_INT_S1_B2TRANS		(1 << 1)
#define hfc_INT_S1_DTRANS		(1 << 2)
#define hfc_INT_S1_B1REC		(1 << 3)
#define hfc_INT_S1_B2REC		(1 << 4)
#define hfc_INT_S1_DREC			(1 << 5)
#define hfc_INT_S1_L1STATE		(1 << 6)
#define hfc_INT_S1_TIMER		(1 << 7)

#define hfc_INT_S2		0x7C
#define hfc_INT_S2_PROC_TRANS		(1 << 0)
#define hfc_INT_S2_GCI_I_CHG		(1 << 1)
#define hfc_INT_S2_GCI_MON_REC		(1 << 2)
#define hfc_INT_S2_IRQ_ENABLE		(1 << 3)
#define hfc_INT_S2_PCISTUCK		(1 << 7)

#define hfc_STATUS		0x70
#define hfc_STATUS_PCI_PROC		(1 << 1)
#define hfc_STATUS_NBUSY		(1 << 2)
#define hfc_STATUS_TIMER_ELAP		(1 << 4)
#define hfc_STATUS_STATINT		(1 << 5)
#define hfc_STATUS_FRAMEINT		(1 << 6)
#define hfc_STATUS_ANYINT		(1 << 7)

#define hfc_STATES		0xC0
#define hfc_STATES_STATE(num)		((num) << 0)
#define hfc_STATES_STATE_MASK		0x0F
#define hfc_STATES_LOAD_STATE		(1 << 4)
#define hfc_STATES_DEACTIVATE		(0x2 << 5)
#define hfc_STATES_ACTIVATE		(0x3 << 5)
#define hfc_STATES_NT_G2_G3		(1 << 7)

#define hfc_SCTRL		0xC4
#define hfc_SCTRL_B1_ENA		(1 << 0)
#define hfc_SCTRL_B2_ENA		(1 << 1)
#define hfc_SCTRL_MODE_TE		(0 << 2)
#define hfc_SCTRL_MODE_NT		(1 << 2)
#define hfc_SCTRL_LOW_PRIO		(1 << 3)
#define hfc_SCTRL_SQ_ENA		(1 << 4)
#define hfc_SCTRL_TEST			(1 << 5)
#define hfc_SCTRL_NONE_CAP		(1 << 6)
#define hfc_SCTRL_PWR_DOWN		(1 << 7)

#define hfc_SCTRL_E		0xC8
#define hfc_SCTRL_E_AUTO_AWAKE		(1 << 0)
#define hfc_SCTRL_E_DBIT_1		(1 << 2)
#define hfc_SCTRL_E_IGNORE_COL		(1 << 3)
#define hfc_SCTRL_E_CHG_B1_B2		(1 << 7)

#define hfc_SCTRL_R		0xCC
#define hfc_SCTRL_R_B1_ENA		(1 << 0)
#define hfc_SCTRL_R_B2_ENA		(1 << 1)

#define hfc_SQ_REC		0xD0
#define hfc_SQ_REC_BITS(num)		(((num) & 0x0f) >> 0)
#define hfc_SQ_RECV_COMPLETE		(1 << 4)
#define hfc_SQ_SEND_READY		(1 << 7)

#define hfc_SQ_SEND		0xD0
#define hfc_SQ_SEND_BITS(num)		(((num) & 0x0f) << 0)

#define hfc_CLKDEL		0xDC
#define hfc_CLKDEL_ST_CLK_DLY(num)	((num) << 0)
#define hfc_CLKDEL_ST_SMPL(num)		((num) << 4)

#define hfc_B1_REC		0xF0
#define hfc_B1_SEND		0xF0
#define hfc_B2_REC		0xF4
#define hfc_B2_SEND		0xF4
#define hfc_D_REC		0xF8
#define hfc_D_SEND		0xF8
#define hfc_E_REC		0xFC

/* bits in the __SSL and __RSL registers */
#define	hfc_SRSL_STIO		0x40
#define hfc_SRSL_ENABLE		0x80
#define hfc_SRCL_SLOT_MASK	0x1f

#endif

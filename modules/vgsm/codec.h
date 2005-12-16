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

#ifndef _VGSM_CODEC_H
#define _VGSM_CODEC_H

#define VGSM_CODEC_CONFIG	0x00
#define VGSM_CODEC_CONFIG_PD0		(1 << 0)
#define VGSM_CODEC_CONFIG_PD1		(1 << 1)
#define VGSM_CODEC_CONFIG_PD2		(1 << 2)
#define VGSM_CODEC_CONFIG_PD3		(1 << 3)
#define VGSM_CODEC_CONFIG_STA		(1 << 4)
#define VGSM_CODEC_CONFIG_AMU_ULAW	(0 << 5)
#define VGSM_CODEC_CONFIG_AMU_ALAW	(1 << 5)
#define VGSM_CODEC_CONFIG_LIN		(1 << 6)
#define VGSM_CODEC_CONFIG_RES		(1 << 7)

#define VGSM_CODEC_DIR_0	0x01
#define VGSM_CODEC_DIR_0_IO_0		(1 << 0)
#define VGSM_CODEC_DIR_0_IO_1		(1 << 1)
#define VGSM_CODEC_DIR_0_IO_2		(1 << 2)
#define VGSM_CODEC_DIR_0_IO_3		(1 << 3)
#define VGSM_CODEC_DIR_0_IO_4		(1 << 4)
#define VGSM_CODEC_DIR_0_IO_5		(1 << 5)
#define VGSM_CODEC_DIR_0_IO_6		(1 << 6)
#define VGSM_CODEC_DIR_0_IO_7		(1 << 7)

#define VGSM_CODEC_DIR_1	0x02
#define VGSM_CODEC_DIR_0_IO_8		(1 << 0)
#define VGSM_CODEC_DIR_0_IO_9		(1 << 1)
#define VGSM_CODEC_DIR_0_IO_10		(1 << 2)
#define VGSM_CODEC_DIR_0_IO_11		(1 << 3)

#define VGSM_CODEC_DATA0_0	0x03
#define VGSM_CODEC_DATA0_0_D0_0		(1 << 0)
#define VGSM_CODEC_DATA0_0_D0_1		(1 << 1)
#define VGSM_CODEC_DATA0_0_D0_2		(1 << 2)
#define VGSM_CODEC_DATA0_0_D0_3		(1 << 3)
#define VGSM_CODEC_DATA0_0_D0_4		(1 << 4)
#define VGSM_CODEC_DATA0_0_D0_5		(1 << 5)
#define VGSM_CODEC_DATA0_0_D0_6		(1 << 6)
#define VGSM_CODEC_DATA0_0_D0_7		(1 << 7)

#define VGSM_CODEC_DATA0_1	0x04
#define VGSM_CODEC_DATA0_1_D0_8		(1 << 0)
#define VGSM_CODEC_DATA0_1_D0_9		(1 << 1)
#define VGSM_CODEC_DATA0_1_D0_10	(1 << 2)
#define VGSM_CODEC_DATA0_1_D0_11	(1 << 3)

#define VGSM_CODEC_DATA1_0	0x05
#define VGSM_CODEC_DATA1_0_D0_0		(1 << 0)
#define VGSM_CODEC_DATA1_0_D0_1		(1 << 1)
#define VGSM_CODEC_DATA1_0_D0_2		(1 << 2)
#define VGSM_CODEC_DATA1_0_D0_3		(1 << 3)
#define VGSM_CODEC_DATA1_0_D0_4		(1 << 4)
#define VGSM_CODEC_DATA1_0_D0_5		(1 << 5)
#define VGSM_CODEC_DATA1_0_D0_6		(1 << 6)
#define VGSM_CODEC_DATA1_0_D0_7		(1 << 7)

#define VGSM_CODEC_DATA1_1	0x06
#define VGSM_CODEC_DATA1_1_D1_8		(1 << 0)
#define VGSM_CODEC_DATA1_1_D1_9		(1 << 1)
#define VGSM_CODEC_DATA1_1_D1_10	(1 << 2)
#define VGSM_CODEC_DATA1_1_D1_11	(1 << 3)

#define VGSM_CODEC_DATA2_0	0x07
#define VGSM_CODEC_DATA2_0_D1_0		(1 << 0)
#define VGSM_CODEC_DATA2_0_D1_1		(1 << 1)
#define VGSM_CODEC_DATA2_0_D1_2		(1 << 2)
#define VGSM_CODEC_DATA2_0_D1_3		(1 << 3)
#define VGSM_CODEC_DATA2_0_D1_4		(1 << 4)
#define VGSM_CODEC_DATA2_0_D1_5		(1 << 5)
#define VGSM_CODEC_DATA2_0_D1_6		(1 << 6)
#define VGSM_CODEC_DATA2_0_D1_7		(1 << 7)

#define VGSM_CODEC_DATA2_1	0x08
#define VGSM_CODEC_DATA2_1_D2_8		(1 << 0)
#define VGSM_CODEC_DATA2_1_D2_9		(1 << 1)
#define VGSM_CODEC_DATA2_1_D2_10	(1 << 2)
#define VGSM_CODEC_DATA2_1_D2_11	(1 << 3)

#define VGSM_CODEC_DATA3_0	0x09
#define VGSM_CODEC_DATA3_0_D3_0		(1 << 0)
#define VGSM_CODEC_DATA3_0_D3_1		(1 << 1)
#define VGSM_CODEC_DATA3_0_D3_2		(1 << 2)
#define VGSM_CODEC_DATA3_0_D3_3		(1 << 3)
#define VGSM_CODEC_DATA3_0_D3_4		(1 << 4)
#define VGSM_CODEC_DATA3_0_D3_5		(1 << 5)
#define VGSM_CODEC_DATA3_0_D3_6		(1 << 6)
#define VGSM_CODEC_DATA3_0_D3_7		(1 << 7)

#define VGSM_CODEC_DATA3_1	0x0a
#define VGSM_CODEC_DATA3_1_D3_8		(1 << 0)
#define VGSM_CODEC_DATA3_1_D3_9		(1 << 1)
#define VGSM_CODEC_DATA3_1_D3_10	(1 << 2)
#define VGSM_CODEC_DATA3_1_D3_11	(1 << 3)

#define VGSM_CODEC_GTX0		0x0b
#define VGSM_CODEC_GTX1		0x0c
#define VGSM_CODEC_GTX2		0x0d
#define VGSM_CODEC_GTX3		0x0e
#define VGSM_CODEC_GRX0		0x0f
#define VGSM_CODEC_GRX1		0x10
#define VGSM_CODEC_GRX2		0x11
#define VGSM_CODEC_GRX3		0x12

#define VGSM_CODEC_DXA0		0x13
#define VGSM_CODEC_DXA0_ENA		(1 << 7)
#define VGSM_CODEC_DXA0_TS(val)		((val) << 0)

#define VGSM_CODEC_DXA1		0x14
#define VGSM_CODEC_DXA1_ENA		(1 << 7)
#define VGSM_CODEC_DXA1_TS(val)		((val) << 0)

#define VGSM_CODEC_DXA2		0x15
#define VGSM_CODEC_DXA2_ENA		(1 << 7)
#define VGSM_CODEC_DXA2_TS(val)		((val) << 0)

#define VGSM_CODEC_DXA3		0x16
#define VGSM_CODEC_DXA3_ENA		(1 << 7)
#define VGSM_CODEC_DXA3_TS(val)		((val) << 0)

#define VGSM_CODEC_DRA0		0x17
#define VGSM_CODEC_DRA0_ENA		(1 << 7)
#define VGSM_CODEC_DRA0_TS(val)		((val) << 0)

#define VGSM_CODEC_DRA1		0x18
#define VGSM_CODEC_DRA1_ENA		(1 << 7)
#define VGSM_CODEC_DRA1_TS(val)		((val) << 0)

#define VGSM_CODEC_DRA2		0x19
#define VGSM_CODEC_DRA2_ENA		(1 << 7)
#define VGSM_CODEC_DRA2_TS(val)		((val) << 0)

#define VGSM_CODEC_DRA3		0x1a
#define VGSM_CODEC_DRA3_ENA		(1 << 7)
#define VGSM_CODEC_DRA3_TS(val)		((val) << 0)

#define VGSM_CODEC_PCMSH	0x1b
#define VGSM_CODEC_PCMSH_RS(val)	((val) << 0)
#define VGSM_CODEC_PCMSH_XS(val)	((val) << 4)

#define VGSM_CODEC_DMASK_0	0x1c
#define VGSM_CODEC_DMASK_0_MD0		(1 << 0)
#define VGSM_CODEC_DMASK_0_MD1		(1 << 1)
#define VGSM_CODEC_DMASK_0_MD2		(1 << 2)
#define VGSM_CODEC_DMASK_0_MD3		(1 << 3)
#define VGSM_CODEC_DMASK_0_MD4		(1 << 4)
#define VGSM_CODEC_DMASK_0_MD5		(1 << 5)
#define VGSM_CODEC_DMASK_0_MD6		(1 << 6)
#define VGSM_CODEC_DMASK_0_MD7		(1 << 7)

#define VGSM_CODEC_DMASK_1	0x1d
#define VGSM_CODEC_DMASK_1_MD8		(1 << 0)
#define VGSM_CODEC_DMASK_1_MD9		(1 << 1)
#define VGSM_CODEC_DMASK_1_MD10		(1 << 2)
#define VGSM_CODEC_DMASK_1_MD11		(1 << 3)

#define VGSM_CODEC_CMASK	0x1e
#define VGSM_CODEC_CMASK_MC0		(1 << 0)
#define VGSM_CODEC_CMASK_MC1		(1 << 1)
#define VGSM_CODEC_CMASK_MC2		(1 << 2)
#define VGSM_CODEC_CMASK_MC3		(1 << 3)

#define VGSM_CODEC_PCHK_A	0x1f
#define VGSM_CODEC_PCHK_B	0x20

#define VGSM_CODEC_INT		0x21
#define VGSM_CODEC_INT_ID0		(1 << 0)
#define VGSM_CODEC_INT_ID1		(1 << 1)
#define VGSM_CODEC_INT_ID2		(1 << 2)
#define VGSM_CODEC_INT_ID3		(1 << 3)
#define VGSM_CODEC_INT_ICKF		(1 << 4)

#define VGSM_CODEC_ALARM	0x22
#define VGSM_CODEC_ALARM_CKF		(1 << 0)
#define VGSM_CODEC_ALARM_POR		(1 << 1)

#define VGSM_CODEC_AMASK	0x23
#define VGSM_CODEC_AMASK_MCF		(1 << 0)

#define VGSM_CODEC_LOOPB	0x24
#define VGSM_CODEC_LOOPB_AL0		(1 << 0)
#define VGSM_CODEC_LOOPB_AL1		(1 << 1)
#define VGSM_CODEC_LOOPB_AL2		(1 << 2)
#define VGSM_CODEC_LOOPB_AL3		(1 << 3)
#define VGSM_CODEC_LOOPB_DL0		(1 << 5)
#define VGSM_CODEC_LOOPB_DL1		(1 << 6)
#define VGSM_CODEC_LOOPB_DL2		(1 << 7)
#define VGSM_CODEC_LOOPB_DL3		(1 << 8)

#define VGSM_CODEC_TXG		0x25
#define VGSM_CODEC_TXG_XG0		(1 << 0)
#define VGSM_CODEC_TXG_XG1		(1 << 1)
#define VGSM_CODEC_TXG_XG2		(1 << 2)
#define VGSM_CODEC_TXG_XG3		(1 << 3)

#define VGSM_CODEC_RXG10	0x26
#define VGSM_CODEC_RXG10_0_MUTE		(0x0 << 0)
#define VGSM_CODEC_RXG10_0_13_98	(0x1 << 0)
#define VGSM_CODEC_RXG10_0_7_96		(0x2 << 0)
#define VGSM_CODEC_RXG10_0_4_44		(0x3 << 0)
#define VGSM_CODEC_RXG10_0_1_94		(0x4 << 0)
#define VGSM_CODEC_RXG10_0_0		(0x5 << 0)

#define VGSM_CODEC_RXG10_1_MUTE		(0x0 << 0)
#define VGSM_CODEC_RXG10_1_13_98	(0x1 << 0)
#define VGSM_CODEC_RXG10_1_7_96		(0x2 << 0)
#define VGSM_CODEC_RXG10_1_4_44		(0x3 << 0)
#define VGSM_CODEC_RXG10_1_1_94		(0x4 << 0)
#define VGSM_CODEC_RXG10_1_0		(0x5 << 0)

#define VGSM_CODEC_RXG32		0x27
#define VGSM_CODEC_RXG32_2_MUTE		(0x0 << 0)
#define VGSM_CODEC_RXG32_2_13_98	(0x1 << 0)
#define VGSM_CODEC_RXG32_2_7_96		(0x2 << 0)
#define VGSM_CODEC_RXG32_2_4_44		(0x3 << 0)
#define VGSM_CODEC_RXG32_2_1_94		(0x4 << 0)
#define VGSM_CODEC_RXG32_2_0		(0x5 << 0)

#define VGSM_CODEC_RXG32_3_MUTE		(0x0 << 0)
#define VGSM_CODEC_RXG32_3_13_98	(0x1 << 0)
#define VGSM_CODEC_RXG32_3_7_96		(0x2 << 0)
#define VGSM_CODEC_RXG32_3_4_44		(0x3 << 0)
#define VGSM_CODEC_RXG32_3_1_94		(0x4 << 0)
#define VGSM_CODEC_RXG32_3_0		(0x5 << 0)

#define VGSM_CODEC_SRIC		0x31

#endif

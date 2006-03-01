/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_REGS_H
#define _HFC_REGS_H

#define hfc_R_CIRM		0x00
#define hfc_R_CIRM_V_IRQ_SEL_NONE		(0x0 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ0		(0x1 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ1		(0x2 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ2		(0x3 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ3		(0x4 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ4		(0x5 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ5		(0x6 << 0)
#define hfc_R_CIRM_V_IRQ_SEL_IRQ6		(0x7 << 0)
#define hfc_R_CIRM_V_SRES			(1 << 3)
#define hfc_R_CIRM_V_HFC_RES			(1 << 4)
#define hfc_R_CIRM_V_PCM_RES			(1 << 5)
#define hfc_R_CIRM_V_E1_RES			(1 << 6)
#define hfc_R_CIRM_V_RLD_EPR			(1 << 7)

#define hfc_R_CTRL		0x01
#define hfc_R_CTRL_V_FIFO_LPRIO			(1 << 1)
#define hfc_R_CTRL_V_SLOW_RD			(1 << 2)
#define hfc_R_CTRL_V_EXT_RAM			(1 << 3)
#define hfc_R_CTRL_V_CLK_OFF			(1 << 5)
#define hfc_R_CTRL_V_ST_CLK_DIV_4		(0x0 << 6)
#define hfc_R_CTRL_V_ST_CLK_DIV_8		(0x1 << 6)
#define hfc_R_CTRL_V_ST_CLK_DIV_1		(0x2 << 6)
#define hfc_R_CTRL_V_ST_CLK_DIV_2		(0x3 << 6)

#define hfc_R_BRG_PCM_CFG	0x02
#define hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV2	(0 << 5)
#define hfc_R_BRG_PCM_CFG_V_PCM_CLK_DIV4	(1 << 5)

#define hfc_R_RAM_ADDR0		0x08
#define hfc_R_RAM_ADDR1		0x09
#define hfc_R_RAM_ADDR2		0x0A

#define hfc_R_FIRST_FIFO	0x0B
#define hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_RX	(1 << 0)
#define hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_TX	(0 << 0)
#define hfc_R_FIRST_FIFO_V_FIRST_FIFO_NUM(num)	((num) << 1)

#define hfc_R_RAM_MISC		0x0C
#define hfc_R_RAM_MISC_V_RAM_SZ_32K		(0x0 << 0)
#define hfc_R_RAM_MISC_V_RAM_SZ_128K		(0x1 << 0)
#define hfc_R_RAM_MISC_V_RAM_SZ_512K		(0x2 << 0)
#define hfc_R_RAM_MISC_V_RAM_SZ_MASK		(0x3 << 0)
#define hfc_R_RAM_MISC_V_PWM0_16KHZ		(1 << 5)
#define hfc_R_RAM_MISC_V_PWM1_16KHZ		(1 << 6)
#define hfc_R_RAM_MISC_V_FZ_MD			(1 << 7)

#define hfc_R_FIFO_MD		0x0D
#define hfc_R_FIFO_MD_V_FIFO_MD_00		(0x0 << 0)
#define hfc_R_FIFO_MD_V_FIFO_MD_10		(0x1 << 0)
#define hfc_R_FIFO_MD_V_FIFO_MD_11		(0x2 << 0)
#define hfc_R_FIFO_MD_V_DF_MD_SM		(0x0 << 2)
#define hfc_R_FIFO_MD_V_DF_MD_CSM		(0x1 << 2)
#define hfc_R_FIFO_MD_V_DF_MD_FSM		(0x3 << 2)
#define hfc_R_FIFO_MD_V_FIFO_SZ_00		(0x0 << 4)
#define hfc_R_FIFO_MD_V_FIFO_SZ_01		(0x1 << 4)
#define hfc_R_FIFO_MD_V_FIFO_SZ_10		(0x2 << 4)
#define hfc_R_FIFO_MD_V_FIFO_SZ_11		(0x3 << 4)

#define hfc_A_INC_RES_FIFO	0x0E
#define hfc_A_INC_RES_FIFO_V_INC_F		(1 << 0)
#define hfc_A_INC_RES_FIFO_V_RES_F		(1 << 1)
#define hfc_A_INC_RES_FIFO_V_RES_LOST		(1 << 2)

#define hfc_R_FSM_IDX		0x0F

#define hfc_R_FIFO		0x0F
#define hfc_R_FIFO_V_FIFO_DIR_TX		(0 << 0)
#define hfc_R_FIFO_V_FIFO_DIR_RX		(1 << 0)
#define hfc_R_FIFO_V_FIFO_NUM(num)		((num) << 1)
#define hfc_R_FIFO_V_REV			(1 << 7)

#define hfc_R_SLOT		0x10
#define hfc_R_SLOT_V_SL_DIR_RX			(1 << 0)
#define hfc_R_SLOT_V_SL_DIR_TX			(0 << 0)
#define hfc_R_SLOT_V_SL_NUM(num)		((num) << 1)

#define hfc_R_IRQMSK_MISC	0x11
#define hfc_R_IRQMSK_MISC_V_STA_IRQMSK		(1 << 0)
#define hfc_R_IRQMSK_MISC_V_TI_IRQMSK		(1 << 1)
#define hfc_R_IRQMSK_MISC_V_PROC_IRQMSK		(1 << 2)
#define hfc_R_IRQMSK_MISC_V_DTMF_IRQMSK		(1 << 3)
#define hfc_R_IRQMSK_MISC_V_IRQ1S_MSK		(1 << 4)
#define hfc_R_IRQMSK_MISC_V_SA6_EXT_IRQMSK	(1 << 5)
#define hfc_R_IRQMSK_MISC_V_RX_EOMF_IRQMSK	(1 << 6)
#define hfc_R_IRQMSK_MISC_V_TX_EOMF_IRQMSK	(1 << 7)

#define hfc_R_SCI_MSK		0x12
#define hfc_R_SCI_MSK_V_SCI_MSK_ST0		(1 << 0)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST1		(1 << 1)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST2		(1 << 2)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST3		(1 << 3)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST4		(1 << 4)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST5		(1 << 5)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST6		(1 << 6)
#define hfc_R_SCI_MSK_V_SCI_MSK_ST7		(1 << 7)

#define hfc_R_IRQ_CTRL		0x13
#define hfc_R_IRQ_CTRL_V_FIFO_IRQ		(1 << 0)
#define hfc_R_IRQ_CTRL_V_GLOB_IRQ_EN		(1 << 3)
#define hfc_R_IRQ_CTRL_V_IRQ_POL_LOW		(0 << 4)
#define hfc_R_IRQ_CTRL_V_IRQ_POL_HIGH		(1 << 4)

#define hfc_R_PCM_MD0		0x14
#define hfc_R_PCM_MD0_V_PCM_MD_MASTER		(1 << 0)
#define hfc_R_PCM_MD0_V_PCM_MD_SLAVE		(0 << 0)
#define hfc_R_PCM_MD0_V_C4_POL			(1 << 1)
#define hfc_R_PCM_MD0_V_F0_NEG			(1 << 2)
#define hfc_R_PCM_MD0_V_F0_LEN			(1 << 3)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL0	(0x0 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL1	(0x1 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL2	(0x2 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL3	(0x3 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL4	(0x4 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL5	(0x5 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL6	(0x6 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SL_SEL7	(0x7 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD1	(0x9 << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_PCM_MD2	(0xA << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SH0L		(0xC << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SH0H		(0xD << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SH1L		(0xE << 4)
#define hfc_R_PCM_MD0_V_PCM_IDX_R_SH1H		(0xF << 4)

#define hfc_R_PCM_MD1		0x15
#define hfc_R_PCM_MD1_V_CODEC_MD		(1 << 0)
#define hfc_R_PCM_MD1_V_PLL_ADJ_4		(0x0 << 2)
#define hfc_R_PCM_MD1_V_PLL_ADJ_3		(0x1 << 2)
#define hfc_R_PCM_MD1_V_PLL_ADJ_2		(0x2 << 2)
#define hfc_R_PCM_MD1_V_PLL_ADJ_1		(0x3 << 2)
#define hfc_R_PCM_MD1_V_PCM_DR_2MBIT		(0x0 << 4)
#define hfc_R_PCM_MD1_V_PCM_DR_4MBIT		(0x1 << 4)
#define hfc_R_PCM_MD1_V_PCM_DR_8MBIT		(0x2 << 4)
#define hfc_R_PCM_MD1_V_PCM_LOOP		(1 << 6)

#define hfc_R_PCM_MD2		0x15
#define hfc_R_PCM_MD2_V_SYNC_PLL_V_SYNC_OUT	(0 << 0)
#define hfc_R_PCM_MD2_V_SYNC_PLL_SYNC_O		(1 << 0)
#define hfc_R_PCM_MD2_V_SYNC_SRC_E1		(0 << 1)
#define hfc_R_PCM_MD2_V_SYNC_SRC_SYNC_I		(1 << 1)
#define hfc_R_PCM_MD2_V_SYNC_OUT_E1		(0 << 2)
#define hfc_R_PCM_MD2_V_SYNC_OUT_SYNC_O		(1 << 2)
#define hfc_R_PCM_MD2_V_ICR_FR_TIME_INCR	(1 << 6)
#define hfc_R_PCM_MD2_V_ICR_FR_TIME_DECR	(0 << 6)
#define hfc_R_PCM_MD2_V_EN_PLL			(1 << 7)

#define hfc_R_SH0H		0x15
#define hfc_R_SH1H		0x15
#define hfc_R_SH0L		0x15
#define hfc_R_SH1L		0x15

#define hfc_R_SL_SEL0		0x15
#define hfc_R_SL_SEL0_V_SL_SEL0(num)		((num) << 0)
#define hfc_R_SL_SEL0_V_SH_SEL0			(1 << 7)

#define hfc_R_SL_SEL1		0x15
#define hfc_R_SL_SEL1_V_SL_SEL1(num)		((num) << 0)
#define hfc_R_SL_SEL1_V_SH_SEL1			(1 << 7)

#define hfc_R_SL_SEL2		0x15
#define hfc_R_SL_SEL2_V_SL_SEL2(num)		((num) << 0)
#define hfc_R_SL_SEL2_V_SH_SEL2			(1 << 7)

#define hfc_R_SL_SEL3		0x15
#define hfc_R_SL_SEL3_V_SL_SEL3(num)		((num) << 0)
#define hfc_R_SL_SEL3_V_SH_SEL3			(1 << 7)

#define hfc_R_SL_SEL4		0x15
#define hfc_R_SL_SEL4_V_SL_SEL4(num)		((num) << 0)
#define hfc_R_SL_SEL4_V_SH_SEL4			(1 << 7)

#define hfc_R_SL_SEL5		0x15
#define hfc_R_SL_SEL5_V_SL_SEL5(num)		((num) << 0)
#define hfc_R_SL_SEL5_V_SH_SEL5			(1 << 7)

#define hfc_R_SL_SEL6		0x15
#define hfc_R_SL_SEL6_V_SL_SEL6(num)		((num) << 0)
#define hfc_R_SL_SEL6_V_SH_SEL6			(1 << 7)

#define hfc_R_SL_SEL7		0x15
#define hfc_R_SL_SEL7_V_SL_SEL7(num)		((num) << 0)
#define hfc_R_SL_SEL7_V_SH_SEL7			(1 << 7)

#define hfc_R_CONF_EN		0x18
#define hfc_R_CONF_EN_V_CONF_EN			(1 << 0)
#define hfc_R_CONF_EN_V_ULAW			(1 << 7)

#define hfc_R_TI_WD		0x1A
#define hfc_R_TI_WD_V_EV_TS_250_US		(0x0 << 0)
#define hfc_R_TI_WD_V_EV_TS_500_US		(0x1 << 0)
#define hfc_R_TI_WD_V_EV_TS_1_MS		(0x2 << 0)
#define hfc_R_TI_WD_V_EV_TS_2_MS		(0x3 << 0)
#define hfc_R_TI_WD_V_EV_TS_4_MS		(0x4 << 0)
#define hfc_R_TI_WD_V_EV_TS_8_MS		(0x5 << 0)
#define hfc_R_TI_WD_V_EV_TS_16_MS		(0x6 << 0)
#define hfc_R_TI_WD_V_EV_TS_32_MS		(0x7 << 0)
#define hfc_R_TI_WD_V_EV_TS_64_MS		(0x8 << 0)
#define hfc_R_TI_WD_V_EV_TS_128_MS		(0x9 << 0)
#define hfc_R_TI_WD_V_EV_TS_256_MS		(0xA << 0)
#define hfc_R_TI_WD_V_EV_TS_512_MS		(0xB << 0)
#define hfc_R_TI_WD_V_EV_TS_1_024_S		(0xC << 0)
#define hfc_R_TI_WD_V_EV_TS_2_048_S		(0xD << 0)
#define hfc_R_TI_WD_V_EV_TS_4_096_S		(0xE << 0)
#define hfc_R_TI_WD_V_EV_TS_8_192_S		(0xF << 0)

#define hfc_R_TI_WD_V_WD_TS_2_MS		(0x0 << 4)
#define hfc_R_TI_WD_V_WD_TS_4_MS		(0x1 << 4)
#define hfc_R_TI_WD_V_WD_TS_8_MS		(0x2 << 4)
#define hfc_R_TI_WD_V_WD_TS_16_MS		(0x3 << 4)
#define hfc_R_TI_WD_V_WD_TS_32_MS		(0x4 << 4)
#define hfc_R_TI_WD_V_WD_TS_64_MS		(0x5 << 4)
#define hfc_R_TI_WD_V_WD_TS_128_MS		(0x6 << 4)
#define hfc_R_TI_WD_V_WD_TS_256_MS		(0x7 << 4)
#define hfc_R_TI_WD_V_WD_TS_512_MS		(0x8 << 4)
#define hfc_R_TI_WD_V_WD_TS_1_024_S		(0x9 << 4)
#define hfc_R_TI_WD_V_WD_TS_2_048_S		(0xA << 4)
#define hfc_R_TI_WD_V_WD_TS_4_096_S		(0xB << 4)
#define hfc_R_TI_WD_V_WD_TS_8_192_S		(0xC << 4)
#define hfc_R_TI_WD_V_WD_TS_16_384_S		(0xD << 4)
#define hfc_R_TI_WD_V_WD_TS_32_768_S		(0xE << 4)
#define hfc_R_TI_WD_V_WD_TS_65_536_S		(0xF << 4)

#define hfc_R_BERT_WD_MD	0x1B
#define hfc_R_BERT_WD_MD_V_PAT_SEQ(seq)		((seq) << 0)
#define hfc_R_BERT_WD_MD_V_PAT_SEQ_MASK		0x07
#define hfc_R_BERT_WD_MD_V_BERT_ERR		(1 << 3)
#define hfc_R_BERT_WD_MD_V_AUTO_WD_RES		(1 << 5)
#define hfc_R_BERT_WD_MD_V_WD_RES		(1 << 7)

#define hfc_R_DTMF		0x1C
#define hfc_R_DTMF_V_DTMF_EN			(1 << 0)
#define hfc_R_DTMF_V_HARM_SEL			(1 << 1)
#define hfc_R_DTMF_V_DTMF_RX_CH			(1 << 2)
#define hfc_R_DTMF_V_DTMF_STOP			(1 << 3)
#define hfc_R_DTMF_V_CHBL_SEL			(1 << 4)
#define hfc_R_DTMF_V_RST_DTMF			(1 << 6)
#define hfc_R_DTMF_V_ULAW_SEL			(1 << 7)

#define hfc_R_DTMF_N		0x1D

#define hfc_R_E1_WR_STA		0x20
#define hfc_R_E1_WR_STA_V_E1_SET_STA(num)	((num) << 0)
#define hfc_R_E1_WR_STA_V_E1_LD_STA		(1 << 4)

#define hfc_R_LOS0		0x22
#define hfc_R_LOS1		0x23

#define hfc_R_RX0		0x24
#define hfc_R_RX0_V_RX_CODE_NRZ			(0x0 << 0)
#define hfc_R_RX0_V_RX_CODE_HDB3		(0x1 << 0)
#define hfc_R_RX0_V_RX_CODE_AMI			(0x2 << 0)
#define hfc_R_RX0_V_RX_FBAUD_HALF		(0 << 2)
#define hfc_R_RX0_V_RX_FBAUD_FULL		(1 << 2)
#define hfc_R_RX0_V_RX_CMI			(1 << 3)
#define hfc_R_RX0_V_RX_INV_CMI			(1 << 4)
#define hfc_R_RX0_V_RX_INV_CLK			(1 << 5)
#define hfc_R_RX0_V_RX_INV_DATA			(1 << 6)
#define hfc_R_RX0_V_RX_AIS_ITU			(1 << 7)

#define hfc_R_RX_SL0_CFG0	0x25
#define hfc_R_RX_SL0_CFG0_V_NO_INSYNC		(1 << 0)
#define hfc_R_RX_SL0_CFG0_V_AUTO_RESYNC		(1 << 1)
#define hfc_R_RX_SL0_CFG0_V_AUTO_RECO		(1 << 2)
#define hfc_R_RX_SL0_CFG0_V_NFAS_CON		(1 << 3)
#define hfc_R_RX_SL0_CFG0_V_FAS_LOSS		(1 << 4)
#define hfc_R_RX_SL0_CFG0_V_XCRC_SYNC		(1 << 5)
#define hfc_R_RX_SL0_CFG0_V_MF_RESYNC		(1 << 6)
#define hfc_R_RX_SL0_CFG0_V_RESYNC		(1 << 7)

#define hfc_R_RX_SL0_CFG1	0x26
#define hfc_R_RX_SL0_CFG1_V_RX_MF		(1 << 0)
#define hfc_R_RX_SL0_CFG1_V_RX_MF_SYNC		(1 << 1)
#define hfc_R_RX_SL0_CFG1_V_RX_SL0_RAM		(1 << 2)
#define hfc_R_RX_SL0_CFG1_V_ERR_SIM		(1 << 5)
#define hfc_R_RX_SL0_CFG1_V_RES_NMF		(1 << 6)

#define hfc_R_TX0		0x28
#define hfc_R_TX0_V_TX_CODE_NRZ			(0x0 << 0)
#define hfc_R_TX0_V_TX_CODE_HDB3		(0x1 << 0)
#define hfc_R_TX0_V_TX_CODE_AMI			(0x2 << 0)
#define hfc_R_TX0_V_TX_FBAUD_HALF		(0 << 2)
#define hfc_R_TX0_V_TX_FBAUD_FULL		(1 << 2)
#define hfc_R_TX0_V_TX_CMI			(1 << 3)
#define hfc_R_TX0_V_TX_INV_CMI			(1 << 4)
#define hfc_R_TX0_V_TX_INV_CLK			(1 << 5)
#define hfc_R_TX0_V_TX_INV_DATA			(1 << 6)
#define hfc_R_TX0_V_OUT_EN			(1 << 7)

#define hfc_R_TX1		0x29
#define hfc_R_TX1_V_INV_MARK			(1 << 0)
#define hfc_R_TX1_V_EXCHG			(1 << 1)
#define hfc_R_TX1_V_AIS_OUT			(1 << 2)
#define hfc_R_TX1_V_ATX				(1 << 5)
#define hfc_R_TX1_V_NTRI			(1 << 6)
#define hfc_R_TX1_V_AUTO_ERR_RES		(1 << 7)

#define hfc_R_TX_SL0_CFG0	0x2C
#define hfc_R_TX_SL0_CFG0_V_TRP_FAS		(1 << 0)
#define hfc_R_TX_SL0_CFG0_V_TRP_NFAS		(1 << 1)
#define hfc_R_TX_SL0_CFG0_V_TRP_RAL		(1 << 2)
#define hfc_R_TX_SL0_CFG0_V_TRP_SA4		(1 << 3)
#define hfc_R_TX_SL0_CFG0_V_TRP_SA5		(1 << 4)
#define hfc_R_TX_SL0_CFG0_V_TRP_SA6		(1 << 5)
#define hfc_R_TX_SL0_CFG0_V_TRP_SA7		(1 << 6)
#define hfc_R_TX_SL0_CFG0_V_TRP_SA8		(1 << 7)

#define hfc_R_TX_SL0		0x2D
#define hfc_R_TX_SL0_V_TX_FAS			(1 << 0)
#define hfc_R_TX_SL0_V_TX_NFAS			(1 << 1)
#define hfc_R_TX_SL0_V_TX_RAL			(1 << 2)
#define hfc_R_TX_SL0_V_TX_SA4			(1 << 3)
#define hfc_R_TX_SL0_V_TX_SA5			(1 << 4)
#define hfc_R_TX_SL0_V_TX_SA6			(1 << 5)
#define hfc_R_TX_SL0_V_TX_SA7			(1 << 6)
#define hfc_R_TX_SL0_V_TX_SA8			(1 << 7)

#define hfc_R_TX_SL0_CFG1	0x2E
#define hfc_R_TX_SL0_CFG1_V_TX_MF		(1 << 0)
#define hfc_R_TX_SL0_CFG1_V_TRP_SL0		(1 << 1)
#define hfc_R_TX_SL0_CFG1_V_TX_SL0_RAM		(1 << 2)
#define hfc_R_TX_SL0_CFG1_V_TX_E		(1 << 4)
#define hfc_R_TX_SL0_CFG1_V_INV_E		(1 << 5)
#define hfc_R_TX_SL0_CFG1_V_XS13		(1 << 6)
#define hfc_R_TX_SL0_CFG1_V_XS15		(1 << 7)

#define hfc_R_JATT_CFG		0x2F
#define hfc_R_JATT_CFG_V_JATT_FRQ_EN		(1 << 0)
#define hfc_R_JATT_CFG_V_JATT_PH_EN		(1 << 1)
#define hfc_R_JATT_CFG_V_JATT_FRQ_IV_1024MS	(0x0 << 2)
#define hfc_R_JATT_CFG_V_JATT_FRQ_IV_512MS	(0x1 << 2)
#define hfc_R_JATT_CFG_V_JATT_FRQ_IV_256MS	(0x2 << 2)
#define hfc_R_JATT_CFG_V_JATT_FRQ_IV_128MS	(0x3 << 2)
#define hfc_R_JATT_CFG_V_JATT_PARM_DEFAULT	(0x9 << 4)

#define hfc_R_RX_OFFS		0x30
#define hfc_R_RX_OFFS_V_RX_OFFS(num)		((num) << 0)
#define hfc_R_RX_OFFS_V_RX_INIT			(1 << 2)

#define hfc_R_SYNC_OUT		0x31
#define hfc_R_SYNC_OUT_V_SYNC_E1_RX		(1 << 0)
#define hfc_R_SYNC_OUT_V_IPATS0			(1 << 5)
#define hfc_R_SYNC_OUT_V_IPATS1			(1 << 6)
#define hfc_R_SYNC_OUT_V_IPATS2			(1 << 7)

#define hfc_R_TX_OFFS		0x34
#define hfc_R_TX_OFFS_V_TX_OFFS(num)		((num) << 0)
#define hfc_R_TX_OFFS_V_TX_INIT			(1 << 2)

#define hfc_R_SYNC_CTRL		0x35
#define hfc_R_SYNC_CTRL_V_EXT_CLK_SYNC		(1 << 0)
#define hfc_R_SYNC_CTRL_V_SYNC_OFFS		(1 << 1)
#define hfc_R_SYNC_CTRL_V_PCM_SYNC_SYNC_I	(0 << 2)
#define hfc_R_SYNC_CTRL_V_PCM_SYNC_F0IO		(1 << 2)
#define hfc_R_SYNC_CTRL_V_NEG_CLK		(1 << 3)
#define hfc_R_SYNC_CTRL_V_HCLK			(1 << 4)
#define hfc_R_SYNC_CTRL_V_JATT_AUTO10		(1 << 5)
#define hfc_R_SYNC_CTRL_V_JATT_MAN		(1 << 6)
#define hfc_R_SYNC_CTRL_V_JATT_OFF		(1 << 7)


#define hfc_R_PWM0		0x38
#define hfc_R_PWM1		0x39

#define hfc_R_GPIO_OUT0		0x40
#define hfc_R_GPIO_OUT0_V_GPIO_OUT0		(1 << 0)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT1		(1 << 1)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT2		(1 << 2)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT3		(1 << 3)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT4		(1 << 4)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT5		(1 << 5)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT6		(1 << 6)
#define hfc_R_GPIO_OUT0_V_GPIO_OUT7		(1 << 7)

#define hfc_R_GPIO_OUT1		0x41
#define hfc_R_GPIO_OUT1_V_GPIO_OUT8		(1 << 0)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT9		(1 << 1)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT10		(1 << 2)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT11		(1 << 3)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT12		(1 << 4)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT13		(1 << 5)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT14		(1 << 6)
#define hfc_R_GPIO_OUT1_V_GPIO_OUT15		(1 << 7)

#define hfc_R_GPIO_EN0		0x42
#define hfc_R_GPIO_EN0_V_GPIO_EN0		(1 << 0)
#define hfc_R_GPIO_EN0_V_GPIO_EN1		(1 << 1)
#define hfc_R_GPIO_EN0_V_GPIO_EN2		(1 << 2)
#define hfc_R_GPIO_EN0_V_GPIO_EN3		(1 << 3)
#define hfc_R_GPIO_EN0_V_GPIO_EN4		(1 << 4)
#define hfc_R_GPIO_EN0_V_GPIO_EN5		(1 << 5)
#define hfc_R_GPIO_EN0_V_GPIO_EN6		(1 << 6)
#define hfc_R_GPIO_EN0_V_GPIO_EN7		(1 << 7)

#define hfc_R_GPIO_EN1		0x43
#define hfc_R_GPIO_EN1_V_GPIO_EN8		(1 << 0)
#define hfc_R_GPIO_EN1_V_GPIO_EN9		(1 << 1)
#define hfc_R_GPIO_EN1_V_GPIO_EN10		(1 << 2)
#define hfc_R_GPIO_EN1_V_GPIO_EN11		(1 << 3)
#define hfc_R_GPIO_EN1_V_GPIO_EN12		(1 << 4)
#define hfc_R_GPIO_EN1_V_GPIO_EN13		(1 << 5)
#define hfc_R_GPIO_EN1_V_GPIO_EN14		(1 << 6)
#define hfc_R_GPIO_EN1_V_GPIO_EN15		(1 << 7)

#define hfc_R_GPIO_SEL		0x44
#define hfc_R_GPIO_SEL_V_GPIO_SEL0		(1 << 0)
#define hfc_R_GPIO_SEL_V_GPIO_SEL1		(1 << 1)
#define hfc_R_GPIO_SEL_V_GPIO_SEL2		(1 << 2)
#define hfc_R_GPIO_SEL_V_GPIO_SEL3		(1 << 3)
#define hfc_R_GPIO_SEL_V_GPIO_SEL4		(1 << 4)
#define hfc_R_GPIO_SEL_V_GPIO_SEL5		(1 << 5)
#define hfc_R_GPIO_SEL_V_GPIO_SEL6		(1 << 6)
#define hfc_R_GPIO_SEL_V_GPIO_SEL7		(1 << 7)

#define hfc_R_PWM_MD		0x46
#define hfc_R_PWM_MD_V_EXT_IRQ_EN		(1 << 3)
#define hfc_R_PWM_MD_V_PWM0_MD_DISABLE		(0 << 4)
#define hfc_R_PWM_MD_V_PWM0_MD_PUSH_PULL	(1 << 4)
#define hfc_R_PWM_MD_V_PWM0_MD_PUSH		(2 << 4)
#define hfc_R_PWM_MD_V_PWM0_MD_PULL		(3 << 4)
#define hfc_R_PWM_MD_V_PWM1_MD_DISABLE		(0 << 6)
#define hfc_R_PWM_MD_V_PWM1_MD_PUSH_PULL	(1 << 6)
#define hfc_R_PWM_MD_V_PWM1_MD_PUSH		(2 << 6)
#define hfc_R_PWM_MD_V_PWM1_MD_PULL		(3 << 6)

#define hfc_A_SL_CFG		0xD0
#define hfc_A_SL_CFG_V_CH_SDIR_RX		(1 << 0)
#define hfc_A_SL_CFG_V_CH_SDIR_TX		(0 << 0)
#define hfc_A_SL_CFG_V_CH_NUM(num)		((num) << 1)
#define hfc_A_SL_CFG_V_ROUT_OFF			(0x0 << 6)
#define hfc_A_SL_CFG_V_ROUT_LOOP		(0x1 << 6)
#define hfc_A_SL_CFG_V_ROUT_OUT_STIO1		(0x2 << 6)
#define hfc_A_SL_CFG_V_ROUT_OUT_STIO2		(0x3 << 6)
#define hfc_A_SL_CFG_V_ROUT_IN_STIO2		(0x2 << 6)
#define hfc_A_SL_CFG_V_ROUT_IN_STIO1		(0x3 << 6)

#define hfc_A_CONF		0xD1
#define hfc_A_CONF_V_CONF_NUM(num)		((num) << 0)
#define hfc_A_CONF_V_NOISE_SUPPR_OFF		(0x0 << 3)
#define hfc_A_CONF_V_NOISE_SUPPR_LT_5		(0x1 << 3)
#define hfc_A_CONF_V_NOISE_SUPPR_LT_9		(0x2 << 3)
#define hfc_A_CONF_V_NOISE_SUPPR_LT_16		(0x3 << 3)
#define hfc_A_CONF_V_ATT_LEV_0DB		(0x0 << 5)
#define hfc_A_CONF_V_ATT_LEV_3DB		(0x1 << 5)
#define hfc_A_CONF_V_ATT_LEV_6DB		(0x2 << 5)
#define hfc_A_CONF_V_ATT_LEV_9DB		(0x3 << 5)
#define hfc_A_CONF_V_CONF_SL			(1 << 7)

#define hfc_A_CH_MSK		0xF4

#define hfc_A_CON_HDLC		0xFA
#define hfc_A_CON_HDCL_V_IFF			(1 << 0)
#define hfc_A_CON_HDCL_V_HDLC_TRP_HDLC		(0 << 1)
#define hfc_A_CON_HDCL_V_HDLC_TRP_TRP		(1 << 1)
#define hfc_A_CON_HDCL_V_TRP_IRQ_DISABLED	(0x0 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_64		(0x1 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_128		(0x2 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_256		(0x3 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_512		(0x4 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_1024		(0x5 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_2048		(0x6 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_4096		(0x7 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED	(0x0 << 2)
#define hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED	(0x1 << 2)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM	(0x0 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_PCM			(0x2 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_ST_to_PCM		(0x4 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_ST_to_PCM			(0x6 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST			(0x0 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_PCM		(0x1 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST_ST_from_PCM	(0x2 << 5)
#define hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_PCM_ST_from_PCM	(0x3 << 5)

#define hfc_A_SUBCH_CFG		0xFB
#define hfc_A_SUBCH_CFG_V_BIT_CNT_8		(0x0 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_1		(0x1 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_2		(0x2 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_3		(0x3 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_4		(0x4 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_5		(0x5 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_6		(0x6 << 0)
#define hfc_A_SUBCH_CFG_V_BIT_CNT_7		(0x7 << 0)
#define hfc_A_SUBCH_CFG_V_START_BIT(num)	((num) << 3)
#define hfc_A_SUBCH_CFG_V_LOOP_FIFO		(1 << 6)
#define hfc_A_SUBCH_CFG_V_INV_DATA		(1 << 7)

#define hfc_A_CHANNEL		0xFC
#define hfc_A_CHANNEL_V_CH_FDIR_RX		(1 << 0)
#define hfc_A_CHANNEL_V_CH_FDIR_TX		(0 << 0)
#define hfc_A_CHANNEL_V_CH_FNUM(num)		((num) << 1)

#define hfc_A_FIFO_SEQ		0xFD
#define hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_RX	(1 << 0)
#define hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_TX	(0 << 0)
#define hfc_A_FIFO_SEQ_V_NEXT_FIFO_NUM(num)	((num) << 1)
#define hfc_A_FIFO_SEQ_V_SEQ_END		(1 << 6)

#define hfc_A_IRQ_MSK		0xFF
#define hfc_A_IRQ_MSK_V_IRQ			(1 << 0)
#define hfc_A_IRQ_MSK_V_BERT_EN			(1 << 1)
#define hfc_A_IRQ_MSK_V_MIX_IRQ			(1 << 2)

#define hfc_A_Z12		0x04
#define hfc_A_Z1L		0x04
#define hfc_A_Z1		0x04
#define hfc_A_Z1H		0x05
#define hfc_A_Z2L		0x06
#define hfc_A_Z2		0x06
#define hfc_A_Z2H		0x07
#define hfc_A_F1		0x0C
#define hfc_A_F12		0x0C
#define hfc_A_F2		0x0D

#define hfc_R_IRQ_OVIEW		0x10
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL0		(1 << 0)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL1		(1 << 1)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL2		(1 << 2)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL3		(1 << 3)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL4		(1 << 4)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL5		(1 << 5)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL6		(1 << 6)
#define hfc_R_IRQ_OVIEW_V_IRQ_FIFO_BL7		(1 << 7)

#define hfc_R_IRQ_MISC		0x11
#define hfc_R_IRQ_MISC_V_STA_IRQ		(1 << 0)
#define hfc_R_IRQ_MISC_V_TI_IRQ			(1 << 1)
#define hfc_R_IRQ_MISC_V_IRQ_PROC		(1 << 2)
#define hfc_R_IRQ_MISC_V_DTMF_IRQ		(1 << 3)
#define hfc_R_IRQ_MISC_V_IRQ1S			(1 << 4)
#define hfc_R_IRQ_MISC_V_SA6_EXT_IRQ		(1 << 5)
#define hfc_R_IRQ_MISC_V_RX_EOMF		(1 << 6)
#define hfc_R_IRQ_MISC_V_TX_EOMF		(1 << 7)

#define hfc_R_CONF_OFLOW	0x14
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW0		(1 << 0)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW1		(1 << 1)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW2		(1 << 2)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW3		(1 << 3)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW4		(1 << 4)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW5		(1 << 5)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW6		(1 << 6)
#define hfc_R_CONF_OFLOW_V_CONF_OFLOW7		(1 << 7)

#define hfc_R_RAM_USE		0x15

#define hfc_R_CHIP_ID		0x16
#define hfc_R_CHIP_ID_V_PNP_IRQ(chipid)		(((chipid) & 0x0f) >> 0)
#define hfc_R_CHIP_ID_V_CHIP_ID(chipid)		(((chipid) & 0xf0) >> 4)
#define hfc_R_CHIP_ID_V_CHIP_ID_HFC_E1		0xe

#define hfc_R_BERT_STA		0x17
#define hfc_R_BERT_STA_V_RD_SYNC_SRC_MASK	(0x0f << 0)
#define hfc_R_BERT_STA_V_BERT_SYNC		(1 << 4)
#define hfc_R_BERT_STA_V_BERT_INV_DATA		(1 << 5)

#define hfc_R_F0_CNTL		0x18
#define hfc_R_F0_CNTH		0x19

#define hfc_R_BERT_ECL		0x1A
#define hfc_R_BERT_ECH		0x1B

#define hfc_R_STATUS		0x1C
#define hfc_R_STATUS_V_BUSY			(1 << 0)
#define hfc_R_STATUS_V_PROC			(1 << 1)
#define hfc_R_STATUS_V_LOST_STA			(1 << 3)
#define hfc_R_STATUS_V_SYNC_IN			(1 << 4)
#define hfc_R_STATUS_V_EXT_IRQSTA		(1 << 5)
#define hfc_R_STATUS_V_MISC_IRQSTA		(1 << 6)
#define hfc_R_STATUS_V_FR_IRQSTA		(1 << 7)

#define hfc_R_CHIP_RV		0x1F
#define hfc_R_CHIP_RV_V_CHIP_RV_MASK		0x0f
#define hfc_R_CHIP_RV_V_CHIP_RV(num)		(((num) & 0x0f) >> 0)

#define hfc_R_E1_RD_STA		0x20
#define hfc_R_E1_RD_STA_V_E1_STA(val)		((val) & 0x7)
#define hfc_R_E1_RD_STA_V_ALT_FR_RX		(1 << 6)
#define hfc_R_E1_RD_STA_V_ALT_FR_TX		(1 << 7)

#define hfc_R_SYNC_STA		0x24
#define hfc_R_SYNC_STA_V_RX_STA_NOT_SYNC	(0x0 << 0)
#define hfc_R_SYNC_STA_V_RX_STA_FAS_FOUND	(0x1 << 0)
#define hfc_R_SYNC_STA_V_RX_STA_NFAS_FOUND	(0x2 << 0)
#define hfc_R_SYNC_STA_V_RX_STA_SYNC		(0x3 << 0)
#define hfc_R_SYNC_STA_V_FR_SYNC		(1 << 2)
#define hfc_R_SYNC_STA_V_SIG_LOSS		(1 << 3)
#define hfc_R_SYNC_STA_V_MFA_STA_NO_MFA		(0x0 << 4)
#define hfc_R_SYNC_STA_V_MFA_STA_ONE_MFA	(0x1 << 4)
#define hfc_R_SYNC_STA_V_MFA_STA_TWO_MFA	(0x2 << 4)
#define hfc_R_SYNC_STA_V_AIS			(1 << 6)
#define hfc_R_SYNC_STA_V_NO_MF_SYNC		(1 << 7)

#define hfc_R_RX_SL0_0		0x25
#define hfc_R_RX_SL0_0_V_SI_FAS			(1 << 0)
#define hfc_R_RX_SL0_0_V_SI_NFAS		(1 << 1)
#define hfc_R_RX_SL0_0_V_A			(1 << 2)
#define hfc_R_RX_SL0_0_V_CRC_OK			(1 << 3)
#define hfc_R_RX_SL0_0_V_TX_E1			(1 << 4)
#define hfc_R_RX_SL0_0_V_TX_E2			(1 << 5)
#define hfc_R_RX_SL0_0_V_RX_E1			(1 << 6)
#define hfc_R_RX_SL0_0_V_RX_E2			(1 << 7)

#define hfc_R_RX_SL0_1		0x26
#define hfc_R_RX_SL0_1_V_SA61			(1 << 0)
#define hfc_R_RX_SL0_1_V_SA62			(1 << 1)
#define hfc_R_RX_SL0_1_V_SA63			(1 << 2)
#define hfc_R_RX_SL0_1_V_SA64			(1 << 3)
#define hfc_R_RX_SL0_1_V_SA6_OK			(1 << 6)
#define hfc_R_RX_SL0_1_V_SA6_CHG		(1 << 7)

#define hfc_R_RX_SL0_2		0x27
#define hfc_R_RX_SL0_2_V_SA4			(1 << 0)
#define hfc_R_RX_SL0_2_V_SA5			(1 << 1)
#define hfc_R_RX_SL0_2_V_SA6			(1 << 2)
#define hfc_R_RX_SL0_2_V_SA7			(1 << 3)
#define hfc_R_RX_SL0_2_V_SA8			(1 << 4)

#define hfc_R_JATT_STA		0x2B
#define hfc_R_JATT_STA_V_JATT_ADJ_RESET		(0x0 << 5)
#define hfc_R_JATT_STA_V_JATT_ADJ_FREQ		(0x1 << 5)
#define hfc_R_JATT_STA_V_JATT_ADJ_PHASE		(0x2 << 5)
#define hfc_R_JATT_STA_V_JATT_ADJ_BOTH		(0x3 << 5)

#define hfc_R_SLIP		0x2C
#define hfc_R_SLIP_V_SLIP_RX			(1 << 0)
#define hfc_R_SLIP_V_SLIP_TX			(1 << 4)

#define hfc_R_FAS_ECL		0x30
#define hfc_R_FAS_ECH		0x31
#define hfc_R_VIO_ECL		0x32
#define hfc_R_VIO_ECH		0x33
#define hfc_R_CRC_ECL		0x34
#define hfc_R_CRC_ECH		0x35
#define hfc_R_E_ECL		0x36
#define hfc_R_E_ECH		0x37
#define hfc_R_SA6_VAL13_ECL	0x38
#define hfc_R_SA6_VAL13_ECH	0x39
#define hfc_R_SA6_VAL23_ECL	0x3A
#define hfc_R_SA6_VAL23_ECH	0x3B

#define hfc_R_GPIO_IN0		0x40
#define hfc_R_GPIO_IN0_V_GPIO_IN0		(1 << 0)
#define hfc_R_GPIO_IN0_V_GPIO_IN1		(1 << 1)
#define hfc_R_GPIO_IN0_V_GPIO_IN2		(1 << 2)
#define hfc_R_GPIO_IN0_V_GPIO_IN3		(1 << 3)
#define hfc_R_GPIO_IN0_V_GPIO_IN4		(1 << 4)
#define hfc_R_GPIO_IN0_V_GPIO_IN5		(1 << 5)
#define hfc_R_GPIO_IN0_V_GPIO_IN6		(1 << 6)
#define hfc_R_GPIO_IN0_V_GPIO_IN7		(1 << 7)

#define hfc_R_GPIO_IN1		0x41
#define hfc_R_GPIO_IN1_V_GPIO_IN8		(1 << 0)
#define hfc_R_GPIO_IN1_V_GPIO_IN9		(1 << 1)
#define hfc_R_GPIO_IN1_V_GPIO_IN10		(1 << 2)
#define hfc_R_GPIO_IN1_V_GPIo_IN11		(1 << 3)
#define hfc_R_GPIO_IN1_V_GPIO_IN12		(1 << 4)
#define hfc_R_GPIO_IN1_V_GPIo_IN13		(1 << 5)
#define hfc_R_GPIO_IN1_V_GPIO_IN14		(1 << 6)
#define hfc_R_GPIO_IN1_V_GPIO_IN15		(1 << 7)

#define hfc_R_GPI_IN0		0x44
#define hfc_R_GPI_IN0_V_GPI_IN0			(1 << 0)
#define hfc_R_GPI_IN0_V_GPI_IN1			(1 << 1)
#define hfc_R_GPI_IN0_V_GPI_IN2			(1 << 2)
#define hfc_R_GPI_IN0_V_GPI_IN3			(1 << 3)
#define hfc_R_GPI_IN0_V_GPI_IN4			(1 << 4)
#define hfc_R_GPI_IN0_V_GPI_IN5			(1 << 5)
#define hfc_R_GPI_IN0_V_GPI_IN6			(1 << 6)
#define hfc_R_GPI_IN0_V_GPI_IN7			(1 << 7)

#define hfc_R_GPI_IN1		0x45
#define hfc_R_GPI_IN1_V_GPI_IN8			(1 << 0)
#define hfc_R_GPI_IN1_V_GPI_IN9			(1 << 1)
#define hfc_R_GPI_IN1_V_GPI_IN10		(1 << 2)
#define hfc_R_GPI_IN1_V_GPI_IN11		(1 << 3)
#define hfc_R_GPI_IN1_V_GPI_IN12		(1 << 4)
#define hfc_R_GPI_IN1_V_GPI_IN13		(1 << 5)
#define hfc_R_GPI_IN1_V_GPI_IN14		(1 << 6)
#define hfc_R_GPI_IN1_V_GPI_IN15		(1 << 7)

#define hfc_R_GPI_IN2		0x46
#define hfc_R_GPI_IN2_V_GPI_IN16		(1 << 0)
#define hfc_R_GPI_IN2_V_GPI_IN17		(1 << 1)
#define hfc_R_GPI_IN2_V_GPI_IN18		(1 << 2)
#define hfc_R_GPI_IN2_V_GPI_IN19		(1 << 3)
#define hfc_R_GPI_IN2_V_GPI_IN20		(1 << 4)
#define hfc_R_GPI_IN2_V_GPI_IN21		(1 << 5)
#define hfc_R_GPI_IN2_V_GPI_IN22		(1 << 6)
#define hfc_R_GPI_IN2_V_GPI_IN23		(1 << 7)

#define hfc_R_GPI_IN3		0x47
#define hfc_R_GPI_IN3_V_GPI_IN24		(1 << 0)
#define hfc_R_GPI_IN3_V_GPI_IN25		(1 << 1)
#define hfc_R_GPI_IN3_V_GPI_IN26		(1 << 2)
#define hfc_R_GPI_IN3_V_GPI_IN27		(1 << 3)
#define hfc_R_GPI_IN3_V_GPI_IN28		(1 << 4)
#define hfc_R_GPI_IN3_V_GPI_IN29		(1 << 5)
#define hfc_R_GPI_IN3_V_GPI_IN30		(1 << 6)
#define hfc_R_GPI_IN3_V_GPI_IN31		(1 << 7)

#define hfc_R_INT_DATA		0x88

#define hfc_R_IRQ_FIFO_BL0	0xC8
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO0_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO0_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO1_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO1_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO2_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO2_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO3_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL0_V_IRQ_FIFO3_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL1	0xC9
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO4_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO4_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO5_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO5_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO6_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO6_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO7_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL1_V_IRQ_FIFO7_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL2	0xCA
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO8_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO8_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO9_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO9_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO10_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO10_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO11_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL2_V_IRQ_FIFO11_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL3	0xCB
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO12_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO12_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO13_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO13_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO14_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO14_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO15_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL3_V_IRQ_FIFO15_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL4	0xCC
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO16_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO16_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO17_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO17_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO18_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO18_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO19_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL4_V_IRQ_FIFO19_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL5	0xCD
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO20_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO20_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO21_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO21_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO22_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO22_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO23_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL5_V_IRQ_FIFO23_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL6	0xCE
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO24_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO24_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO25_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO25_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO26_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO26_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO27_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL6_V_IRQ_FIFO27_RX	(1 << 7)

#define hfc_R_IRQ_FIFO_BL7	0xCF
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO28_TX	(1 << 0)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO28_RX	(1 << 1)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO29_TX	(1 << 2)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO29_RX	(1 << 3)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO30_TX	(1 << 4)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO30_RX	(1 << 5)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO31_TX	(1 << 6)
#define hfc_R_IRQ_FIFO_BL7_V_IRQ_FIFO31_RX	(1 << 7)

#define hfc_A_FIFO_DATA2	0x80
#define hfc_A_FIFO_DATA0	0x80
#define hfc_A_FIFO_DATA1	0x80
#define hfc_A_FIFO_DATA2_NOINC	0x84
#define hfc_A_FIFO_DATA0_NOINC	0x84
#define hfc_A_FIFO_DATA1_NOINC	0x84
#define hfc_R_RAM_DATA		0xC0

#endif

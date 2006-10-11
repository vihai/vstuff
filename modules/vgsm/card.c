/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 * Copyright (C) 2005 Massimo Mazzeo
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <linux/kstreamer/link.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"
#include "micro.h"
#include "codec.h"

static void vgsm_read_msg(
	struct vgsm_card *card, struct vgsm_micro_message *msg)
{
	msg->raw[0] = ioread8(card->io_mem + VGSM_PIB_C0);
	msg->raw[1] = ioread8(card->io_mem + VGSM_PIB_C4);
	msg->raw[2] = ioread8(card->io_mem + VGSM_PIB_C8);
	msg->raw[3] = ioread8(card->io_mem + VGSM_PIB_CC);
	msg->raw[4] = ioread8(card->io_mem + VGSM_PIB_D0);
	msg->raw[5] = ioread8(card->io_mem + VGSM_PIB_D4);
	msg->raw[6] = ioread8(card->io_mem + VGSM_PIB_D8);
	msg->raw[7] = ioread8(card->io_mem + VGSM_PIB_DC);	

#if 0
	printk(KERN_DEBUG "RX MSG: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		msg->raw[0],
		msg->raw[1],
		msg->raw[2],
		msg->raw[3],
		msg->raw[4],
		msg->raw[5],
		msg->raw[6],
		msg->raw[7]);
#endif
}

void vgsm_write_msg(
	struct vgsm_card *card, struct vgsm_micro_message *msg)
{
#if 0
	printk(KERN_DEBUG "TX MSG: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		msg->raw[0],
		msg->raw[1],
		msg->raw[2],
		msg->raw[3],
		msg->raw[4],
		msg->raw[5],
		msg->raw[6],
		msg->raw[7]);
#endif

	iowrite8(msg->raw[0], card->io_mem + VGSM_PIB_C0);
	iowrite8(msg->raw[1], card->io_mem + VGSM_PIB_C4);
	iowrite8(msg->raw[2], card->io_mem + VGSM_PIB_C8);
	iowrite8(msg->raw[3], card->io_mem + VGSM_PIB_CC);
	iowrite8(msg->raw[4], card->io_mem + VGSM_PIB_D0);
	iowrite8(msg->raw[5], card->io_mem + VGSM_PIB_D4);
	iowrite8(msg->raw[6], card->io_mem + VGSM_PIB_D8);
	iowrite8(msg->raw[7], card->io_mem + VGSM_PIB_DC);
}

/*
static void vgsm_send_codec_resync(
	struct vgsm_card *card,
	u8 reg_address, 
	u8 reg_data)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_CODEC_SET;
	msg.numbytes = 2;
	
	msg.payload[0] = 0x00;
	msg.payload[1] = 0x00;

	vgsm_send_msg(card, 0, &msg);
}
*/

static void vgsm_send_codec_setreg(
	struct vgsm_card *card,
	u8 reg_address, 
	u8 reg_data)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_CODEC_SET;
	msg.numbytes = 2;
	
	msg.payload[0] = reg_address | 0x40;
	msg.payload[1] = reg_data;

	vgsm_card_lock(card);
	vgsm_send_msg(&card->micros[0], &msg);
	vgsm_card_unlock(card);
}
	
static void vgsm_send_codec_getreg(
	struct vgsm_card *card,
	u8 reg_address)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_CODEC_GET;
	msg.numbytes = 1;
	
	msg.payload[0] = reg_address | 0x80 | 0x40;

	vgsm_send_msg(&card->micros[0], &msg);
}

void vgsm_update_mask0(struct vgsm_card *card)
{
	int i;

	card->regs.mask0 &= ~(VGSM_INT1STAT_WR_REACH_INT |
				VGSM_INT1STAT_WR_REACH_END |
				VGSM_INT1STAT_RD_REACH_INT |
				VGSM_INT1STAT_RD_REACH_END);

	for(i=0; i<card->num_modules; i++) {

		if (card->modules[i]->rx.ks_link.pipeline->status ==
			KS_PIPELINE_STATUS_FLOWING) {

			card->regs.mask0 |=
				VGSM_INT1STAT_WR_REACH_INT |
				VGSM_INT1STAT_WR_REACH_END |
				VGSM_INT1STAT_RD_REACH_INT |
				VGSM_INT1STAT_RD_REACH_END;

			break;
		}
	}

	vgsm_outb(card, VGSM_MASK0, card->regs.mask0);
}

void vgsm_update_codec(struct vgsm_module *module)
{
	struct vgsm_card *card = module->card;

	switch(module->id) {
	case 0:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX0,
					module->tx.codec_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX0,
					module->rx.codec_gain);

		card->regs.codec_loop &= ~(VGSM_CODEC_LOOPB_AL0 |
			       		VGSM_CODEC_LOOPB_DL0);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL0;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL0;
	break;

	case 1:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX1,
					module->tx.codec_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX1,
					module->rx.codec_gain);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL1;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL1;
	break;

	case 2:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX2,
					module->tx.codec_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX2,
					module->rx.codec_gain);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL2;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL2;
	break;

	case 3:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX3,
					module->tx.codec_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX3,
					module->rx.codec_gain);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL3;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL3;
	break;
	default:
		BUG();
	}

	vgsm_send_codec_setreg(card, VGSM_CODEC_LOOPB, card->regs.codec_loop);
}

static inline char escape_unprintable(char c)
{
	if (isprint(c))
		return c;
	else
		return ' ';
}

static void vgsm_card_rx_tasklet(unsigned long data)
{
	struct vgsm_card *card = (struct vgsm_card *)data;
	int i;

	vgsm_card_lock(card);

	for(i=0; i<card->num_modules; i++) {
		struct vgsm_module *module;

		module = card->modules[i];

		if (!test_bit(VGSM_MODULE_STATUS_RX_THROTTLE,
				&module->status)) {

			if (test_and_clear_bit(
					VGSM_MODULE_STATUS_RX_ACK_PENDING,
					&module->status))
				vgsm_module_send_ack(module);
		}
	}

	vgsm_card_unlock(card);
}

static void vgsm_card_tx_tasklet(unsigned long data)
{
	struct vgsm_card *card = (struct vgsm_card *)data;
	int i;

	vgsm_card_lock(card);
	for(i=0; i<card->num_modules; i++) {
		struct vgsm_module *module;

		card->rr_last_module++;

		if (card->rr_last_module >= card->num_modules)
			card->rr_last_module = 0;

		module = card->modules[card->rr_last_module];

		if (kfifo_len(module->tx.fifo)) {
			if (!test_and_set_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING,
							&module->status)) {

				u8 buf[7];
				int bytes_to_send =
				       	__kfifo_get(module->tx.fifo, buf, 7);

				wake_up(&module->tx.wait_queue);

				vgsm_module_send_string(module, buf,
					       bytes_to_send);

				module->ack_timeout_timer.expires =
					jiffies + HZ;
				add_timer(&module->ack_timeout_timer);

				tty_wakeup(module->tty);
			}
		}
	}
	vgsm_card_unlock(card);
}

/* HW initialization */

static int vgsm_initialize_hw(struct vgsm_card *card)
{
	/* Resetting all */
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_DMA_RST |
		VGSM_CNTL_SERIAL_RST |
		VGSM_CNTL_EXTRST);
	mb();
	msleep(100);
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_DMA_RST |
		VGSM_CNTL_SERIAL_RST);
	mb();
	msleep(100);
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_DMA_INT_PERSISTENT |
		VGSM_CNTL_DMA_SELF |
		VGSM_CNTL_PIB_CYCLE_3 |
		VGSM_CNTL_EXTRST);
	mb();

	/* Set all AUX to outputs */
	vgsm_outb(card, VGSM_AUXC,
		VGSM_AUXC_0_IN |
		VGSM_AUXC_1_IN |
		VGSM_AUXC_2_IN |
		VGSM_AUXC_3_IN |
		VGSM_AUXC_4_IN |
		VGSM_AUXC_5_IN |
		VGSM_AUXC_6_IN |
		VGSM_AUXC_7_IN); 

	/* Configure serial port for MSB->LSB operation */
	vgsm_outb(card, VGSM_SERCTL,
		VGSM_SERCTL_SHIFT_IN_MSB_FIRST |
		VGSM_SERCTL_SHIFT_OUT_MSB_FIRST); 

	/* Delay FSC by 0 so it's properly aligned */
	vgsm_outb(card, VGSM_FSCDELAY, 0x0); 

	/* Setup DMA bus addresses on Tiger 320*/

	vgsm_outl(card, VGSM_DMA_WR_START, cpu_to_le32(card->writedma_bus_mem));
	vgsm_outl(card, VGSM_DMA_WR_INT,
		cpu_to_le32(card->writedma_bus_mem + card->writedma_size / 2));
	vgsm_outl(card, VGSM_DMA_WR_END,
		cpu_to_le32(card->writedma_bus_mem + card->writedma_size - 4));

	vgsm_outl(card, VGSM_DMA_RD_START, cpu_to_le32(card->readdma_bus_mem));	
	vgsm_outl(card, VGSM_DMA_RD_INT,
		cpu_to_le32(card->readdma_bus_mem + card->readdma_size / 2));
	vgsm_outl(card, VGSM_DMA_RD_END,
		cpu_to_le32(card->readdma_bus_mem + card->readdma_size - 4));

	/* Clear DMA interrupts */
	vgsm_outb(card, VGSM_INT0STAT, 0x3F);

	/* PIB initialization */
	msleep(10);
	vgsm_outb(card, VGSM_PIB_E0, 0);
	msleep(10);
	vgsm_outb(card, VGSM_PIB_E0, 1);
	msleep(10);
	vgsm_outb(card, VGSM_PIB_E0, 0);

	/* Setting polarity control register */
	vgsm_outb(card, VGSM_AUX_POL, 0x03);

	vgsm_msg(KERN_DEBUG, "VGSM card initialized\n");

	return 0;
}

static inline void vgsm_handle_serial_int(
	struct vgsm_card *card,
	int micro,
	struct vgsm_micro_message *msg)
{
	struct vgsm_module *module;

	if (micro == 0) {
		if (msg->cmd == VGSM_CMD_S0)
			module = card->modules[0];
		else
			module = card->modules[1];
	} else {
		if (msg->cmd == VGSM_CMD_S0)
			module = card->modules[2];
		else
			module = card->modules[3];
	}

#if 0
printk(KERN_DEBUG "Mod %d: MSG: %c%c%c%c%c%c%c\n",
	module->id,
	escape_unprintable(msg->payload[0]),
	escape_unprintable(msg->payload[1]),
	escape_unprintable(msg->payload[2]),
	escape_unprintable(msg->payload[3]),
	escape_unprintable(msg->payload[4]),
	escape_unprintable(msg->payload[5]),
	escape_unprintable(msg->payload[6]));
#endif

	/* Got msg from micro, now send ACK */
	if (msg->cmd_dep != 0)
		printk(KERN_ERR "cmd_dep != 0 ????\n");

	if (test_bit(VGSM_MODULE_STATUS_RUNNING, &module->status)) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
		unsigned char *dest;
		int to_read = tty_prepare_flip_string(
				module->tty, &dest, msg->numbytes);

		memcpy(dest, msg->payload, to_read);
		tty_schedule_flip(module->tty);
#else

		int i;
		for(i=0; i<msg->numbytes; i++) {
			if (module->tty->flip.count >= TTY_FLIPBUF_SIZE)
				tty_flip_buffer_push(module->tty);

			tty_insert_flip_char(module->tty, msg->payload[i],
							TTY_NORMAL);
		}

		tty_flip_buffer_push(module->tty);
#endif
	}

	set_bit(VGSM_MODULE_STATUS_RX_ACK_PENDING, &module->status);

	tasklet_schedule(&card->rx_tasklet);
}

static inline void vgsm_handle_maint_int(
	struct vgsm_card *card,
	int micro,
	struct vgsm_micro_message *msg)
{
	struct vgsm_module *module;

	if (msg->cmd_dep == VGSM_CMD_MAINT_ACK) {
		if (micro == 0) {
			if (msg->numbytes == 0)
				module = card->modules[0];
			else
				module = card->modules[1];
		} else {
			if (msg->numbytes == 0)
				module = card->modules[2];
			else
				module = card->modules[3];
		}

#if 0
printk(KERN_DEBUG "Received ACK from module %d\n\n", module->id);
#endif

		clear_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING,
			&module->status);

		del_timer(&module->ack_timeout_timer);

		tasklet_schedule(&card->tx_tasklet);
	} else if (msg->cmd_dep == VGSM_CMD_MAINT_CODEC_GET) {
/*		printk(KERN_DEBUG "CODEC RESP: %02x = %02x\n",
			msg->payload[0], msg->payload[1]);*/
	} else if (msg->cmd_dep == VGSM_CMD_MAINT_GET_FW_VER) {
		printk(KERN_INFO
			"Micro %d firmware version %c%c%c%c%c%c%c\n",
			micro,
			msg->payload[0],
			msg->payload[1],
			msg->payload[2],
			msg->payload[3],
			msg->payload[4],
			msg->payload[5],
			msg->payload[6]);
	} else if (msg->cmd_dep == VGSM_CMD_MAINT_POWER_GET) {

		if (micro == 0) {
			if (msg->payload[0] & 0x01)
				set_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[0]->status);
			else
				clear_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[0]->status);

			if (msg->payload[0] & 0x02)
				set_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[1]->status);
			else
				clear_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[1]->status);

			complete_all(&card->modules[0]->read_status_completion);
			complete_all(&card->modules[1]->read_status_completion);
		} else {
			if (msg->payload[0] & 0x01)
				set_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[2]->status);
			else
				clear_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[2]->status);

			if (msg->payload[0] & 0x02)
				set_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[3]->status);
			else
				clear_bit(VGSM_MODULE_STATUS_ON,
					&card->modules[3]->status);

			complete_all(&card->modules[2]->read_status_completion);
			complete_all(&card->modules[3]->read_status_completion);
		}
	}
}

static irqreturn_t vgsm_interrupt(int irq, 
	void *dev_id, 
	struct pt_regs *regs)
{
	struct vgsm_card *card = dev_id;
	u8 int0stat;
	u8 int1stat;
	
	if (unlikely(!card)) {
		vgsm_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

	/* When reading this register, immediately write back the value
	* that has been read for correct operation. Cfr. Tiger 320 spec. p.23 */

	int0stat = vgsm_inb(card, VGSM_INT0STAT) & card->regs.mask0;
	/* consider only AUX0 and AUX1 */
	int1stat = vgsm_inb(card, VGSM_INT1STAT) & 0x03;
	mb();
	vgsm_outb(card, VGSM_INT0STAT, int0stat);

	if (!int0stat && !int1stat)
		return IRQ_NONE;

	if (int0stat & VGSM_INT1STAT_WR_REACH_INT) {
		int i;
		for(i=0; i < (card->writedma_size / 2) - 1; i++)
			*(u8 *)(card->writedma_mem + i) = 0x2a;
	}

	if (int0stat & VGSM_INT1STAT_WR_REACH_END) {
		int i;
		for(i=card->writedma_size / 2;
		   i < card->writedma_size - 1; i++)
			*(u8 *)(card->writedma_mem + i) = 0x2a;
	}

	if (int0stat & VGSM_INT1STAT_PCI_MASTER_ABORT)
		vgsm_msg(KERN_WARNING, "PCI master abort\n");

	if (int0stat & VGSM_INT1STAT_PCI_TARGET_ABORT)
		vgsm_msg(KERN_WARNING, "PCI target abort\n");

	if (int1stat) {
		struct vgsm_micro_message msg;

		int micro;
		if (int1stat == 0x01)
			micro = 0;
		else if (int1stat == 0x02)
			micro = 1;
		else {
			WARN_ON(1);
			goto err_unexpected_micro;
		}

		vgsm_outb(card, VGSM_PIB_E0, 0x1); /* Acquire buffer, E0 = 1 */
		mb();
		vgsm_read_msg(card, &msg); /* Read message */
		mb();
		vgsm_outb(card, VGSM_PIB_E0, 0x0); /* Release buffer E0 = 0 */

		if (msg.cmd == VGSM_CMD_S0 || msg.cmd == VGSM_CMD_S1)
			vgsm_handle_serial_int(card, micro, &msg);
		else if (msg.cmd == VGSM_CMD_MAINT)
			vgsm_handle_maint_int(card, micro, &msg);
		else if (msg.cmd == VGSM_CMD_FW_UPGRADE) {
			if (msg.cmd_dep == 0)
				printk(KERN_CRIT "FW upgrade ready!\n");
			else if (msg.cmd_dep == 1)
				printk(KERN_CRIT "FW upgrade complete!\n");
			else if (msg.cmd_dep == 7)
				printk(KERN_CRIT "FW upgrade failed!\n");

			complete_all(&card->micros[micro].fw_upgrade_ready);
		}
	}

err_unexpected_micro:

	return IRQ_HANDLED;
}

static void vgsm_maint_timer(unsigned long data)
{
	struct vgsm_card *card = (struct vgsm_card *)data;

//	vgsm_card_lock(card);
//	vgsm_send_codec_getreg(card, VGSM_CODEC_CONFIG);
//	vgsm_send_codec_getreg(card, VGSM_CODEC_ALARM);
//	udelay(5000);
/*	vgsm_send_codec_getreg(card, VGSM_CODEC_GTX0);
	udelay(5000);
	vgsm_send_codec_getreg(card, VGSM_CODEC_GTX1);
	udelay(5000);
	vgsm_send_codec_getreg(card, VGSM_CODEC_GTX2);
	udelay(5000);
	vgsm_send_codec_getreg(card, VGSM_CODEC_GTX3);*/
//	udelay(5000);
//	vgsm_module_send_power_get(card->modules[0]);
//	vgsm_module_send_power_get(card->modules[1]);
//	vgsm_module_send_power_get(card->modules[2]);
//	vgsm_module_send_power_get(card->modules[3]);
//	vgsm_card_unlock(card);

	if (!test_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags))
		mod_timer(&card->maint_timer, jiffies + 5 * HZ);
}

void vgsm_codec_reset(
	struct vgsm_card *card)
{
	/* Reset codec */
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_CONFIG,
		VGSM_CODEC_CONFIG_RES);
	mb();

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_CONFIG,
		VGSM_CODEC_CONFIG_AMU_ALAW);
//		VGSM_CODEC_CONFIG_STA);
	mb();
	
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DIR_0,
		VGSM_CODEC_DIR_0_IO_0);

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_PCMSH,
		VGSM_CODEC_PCMSH_RS(1) | VGSM_CODEC_PCMSH_XS(0));

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DXA0,
		VGSM_CODEC_DXA0_ENA | VGSM_CODEC_DXA0_TS(0));

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DXA1,
		VGSM_CODEC_DXA1_ENA | VGSM_CODEC_DXA1_TS(1));

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DXA2,
		VGSM_CODEC_DXA2_ENA | VGSM_CODEC_DXA2_TS(2));

	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DXA3,
		VGSM_CODEC_DXA3_ENA | VGSM_CODEC_DXA3_TS(3));
	
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DRA0,
		VGSM_CODEC_DRA0_ENA | VGSM_CODEC_DRA0_TS(0));
	
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DRA1,
		VGSM_CODEC_DRA1_ENA | VGSM_CODEC_DRA1_TS(1));
	
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DRA2,
		VGSM_CODEC_DRA2_ENA | VGSM_CODEC_DRA2_TS(2));
	
	vgsm_send_codec_setreg(card,
		VGSM_CODEC_DRA3,
		VGSM_CODEC_DRA3_ENA | VGSM_CODEC_DRA3_TS(3));

	vgsm_send_codec_setreg(card, VGSM_CODEC_RXG10,
		VGSM_CODEC_RXG10_CH0_0 | VGSM_CODEC_RXG10_CH1_0);
	vgsm_send_codec_setreg(card, VGSM_CODEC_RXG32,
		VGSM_CODEC_RXG32_CH2_0 | VGSM_CODEC_RXG32_CH3_0);
}

static struct vgsm_card *vgsm_card_alloc(void)
{
	struct vgsm_card *card;
	
	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return NULL;

	return card;
}

static void vgsm_card_release(struct kref *kref)
{
	struct vgsm_card *card = container_of(kref, struct vgsm_card, kref);

	kfree(card);
}

struct vgsm_card *vgsm_card_get(struct vgsm_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void vgsm_card_put(struct vgsm_card *card)
{
	kref_put(&card->kref, vgsm_card_release);
}


static void vgsm_card_init(
	struct vgsm_card *card,
	struct pci_dev *pci_dev,
	int id)
{
	int i;

	memset(card, 0, sizeof(*card));

	kref_init(&card->kref);

	card->pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	card->id = id;

	spin_lock_init(&card->lock);

	tasklet_init(&card->rx_tasklet, vgsm_card_rx_tasklet,
			(unsigned long)card);

	tasklet_init(&card->tx_tasklet, vgsm_card_tx_tasklet,
			(unsigned long)card);

	init_timer(&card->maint_timer);
	card->maint_timer.function = vgsm_maint_timer;
	card->maint_timer.data = (unsigned long)card;

	card->readdma_size = vgsm_DMA_SAMPLES * 4;
	card->writedma_size = vgsm_DMA_SAMPLES * 4;

	card->num_micros = 2;
	for (i=0; i<card->num_micros; i++)
		vgsm_micro_init(&card->micros[i], card, i);

	card->num_modules = 4;
	for (i=0; i<card->num_modules; i++)
		card->modules[i] = NULL;
}

int vgsm_card_probe(
	struct pci_dev *pci_dev, 
	const struct pci_device_id *ent)
{
	struct vgsm_card *card;
	static int numcards; /* Change with atomic? */
	int err;
	int i;

	card = vgsm_card_alloc();
	if (!card) {
		err = -ENOMEM;
		goto err_card_alloc;
	}

	vgsm_card_init(card, pci_dev, numcards++);

	/* From here on vgsm_msg_card may be used */

	err = pci_enable_device(pci_dev);
	if (err < 0) {
		goto err_pci_enable_device;
	}

	/* Interrogate the PCI layer to see if the PCI controller on the
	   machine can properly support the DMA addressing limitation */

	err = pci_set_dma_mask(pci_dev, PCI_DMA_32BIT);
	if (err < 0) {
		vgsm_msg_card(card, KERN_ERR,
			     "No suitable DMA configuration available.\n");
		goto err_pci_set_dma_mask;
	}

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(pci_dev, vgsm_DRIVER_NAME);
	if(err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev, 1);
	if (!card->io_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_noiobase;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->io_mem = ioremap(card->io_bus_mem, vgsm_PCI_MEM_SIZE);

	if(!card->io_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap;
	}


	vgsm_msg(KERN_DEBUG, "vgsm card found at 0x%08lx mapped at %p\n",
		card->io_bus_mem, card->io_mem);


	/* Allocate enough DMA memory for 4 modules, receive and transmit.  
	* Each sample written by PCI bridge is 32 bits, 8 bits/module */

	/* READ DMA */
	card->readdma_mem = pci_alloc_consistent(
				pci_dev, card->readdma_size,
				&card->readdma_bus_mem);

	if (!card->readdma_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "unable to allocate READ DMA memory!\n");
		err = -ENOMEM;
		goto err_alloc_readdma;
	}

	memset(card->readdma_mem, 0, card->readdma_size);

	/* WRITE DMA */
	card->writedma_mem = pci_alloc_consistent(
				pci_dev, card->writedma_size,
				&card->writedma_bus_mem);

	if (!card->writedma_mem) {
		vgsm_msg_card(card, KERN_CRIT,
				"unable to allocate WRITE DMA memory!\n");
		err = -ENOMEM;
		goto err_alloc_writedma;
	}

	memset(card->writedma_mem, 0, card->writedma_size);

	/* Requesting IRQ */
	err = request_irq(card->pci_dev->irq, &vgsm_interrupt,
			  SA_SHIRQ, vgsm_DRIVER_NAME, card);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "unable to register irq\n");
		goto err_request_irq;
	}

	{
	/* Initialize buffers with 1 kHz tone */
	u8 khz[] = { 0x34, 0x21, 0x21, 0x34, 0xb4, 0xa1, 0xa1, 0xb4 };
	for (i=0; i<vgsm_DMA_SAMPLES; i++) {
		*(u8 *)(card->writedma_mem + i * 4 + 0) = khz[i % 8];
		*(u8 *)(card->writedma_mem + i * 4 + 1) = khz[i % 8];
		*(u8 *)(card->writedma_mem + i * 4 + 2) = khz[i % 8];
		*(u8 *)(card->writedma_mem + i * 4 + 3) = khz[i % 8];
	}
	}

	for (i=0; i<card->num_modules; i++) {
		char name[8];

		snprintf(name, sizeof(name), "gsm%d", i);

		card->modules[i] = vgsm_module_alloc(
				card, &card->micros[i/2], i, name);
		if (!card->modules[i]) {
			err = -ENOMEM;
			goto err_modules_alloc;
		}
	}

	vgsm_initialize_hw(card);

	/* Enable interrupts */
	card->regs.mask0 = 0;
	vgsm_outb(card, VGSM_MASK0, card->regs.mask0);
	vgsm_outb(card, VGSM_MASK1, 0x03);

	/* Start DMA */
	vgsm_outb(card, VGSM_DMA_OPER, VGSM_DMA_OPER_DMA_ENABLE);

	vgsm_module_send_set_padding_timeout(card->modules[0], 1);
	vgsm_module_send_set_padding_timeout(card->modules[1], 1);
	vgsm_module_send_set_padding_timeout(card->modules[2], 1);
	vgsm_module_send_set_padding_timeout(card->modules[3], 1);

	vgsm_send_get_fw_ver(&card->micros[0]);
	vgsm_send_get_fw_ver(&card->micros[1]);

	vgsm_codec_reset(card);

	vgsm_codec_reset(card);

	for (i=0; i<card->num_modules; i++) {
		err = vgsm_module_register(card->modules[i]);
		if (err < 0)
			goto err_module_register;
	}

	spin_lock(&vgsm_cards_list_lock);
	list_add_tail(&card->cards_list_node, &vgsm_cards_list);
	spin_unlock(&vgsm_cards_list_lock);

	card->maint_timer.expires = jiffies + 5 * HZ;
	add_timer(&card->maint_timer);

	return 0;

	free_irq(pci_dev->irq, card);
err_request_irq:
	pci_free_consistent(pci_dev, card->writedma_size,
	card->writedma_mem, card->writedma_bus_mem);
err_alloc_writedma:
	pci_free_consistent(pci_dev, card->readdma_size,
	card->readdma_mem, card->readdma_bus_mem);		
err_alloc_readdma:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
err_pci_set_dma_mask:
err_pci_enable_device:
// TODO Unregister only registered modules
//	vgsm_module_unregister(&card->modules[0]);
//	vgsm_module_dealloc(&card->modules[0]);
err_modules_alloc:
err_module_register:
	kfree(card);
err_card_alloc:

	return err;	
}

void vgsm_card_remove(struct vgsm_card *card)
{
	int i;
	int shutting_down = FALSE;

	/* Clean up any allocated resources and stuff here */

	vgsm_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n", card->io_mem);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

	del_timer_sync(&card->maint_timer);

	spin_lock(&vgsm_cards_list_lock);
	list_del(&card->cards_list_node);
	spin_unlock(&vgsm_cards_list_lock);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_unregister(card->modules[i]);

		vgsm_card_lock(card);
		vgsm_module_send_power_get(card->modules[i]);
		vgsm_card_unlock(card);

		wait_for_completion_timeout(
			&card->modules[i]->read_status_completion, 2 * HZ);

		if (test_bit(VGSM_MODULE_STATUS_ON,
						&card->modules[i]->status)) {

			/* Force an emergency shutdown of the application did
			 * not do its duty
			 */

			vgsm_card_lock(card);
			vgsm_module_send_onoff(card->modules[i],
				VGSM_CMD_MAINT_ONOFF_EMERG_OFF);
			vgsm_card_unlock(card);

			shutting_down = TRUE;

			vgsm_msg_card(card, KERN_NOTICE,
				"Module %d has not been shut down, forcing"
				" emergency shutdown\n",
				card->modules[i]->id);
		}
	}

	if (shutting_down) {

		msleep(3200);

		for(i=0; i<card->num_modules; i++) {
			vgsm_card_lock(card);
			vgsm_module_send_onoff(card->modules[i], 0);
			vgsm_card_unlock(card);
		}
	}

	/* Disable IRQs */
	vgsm_outb(card, VGSM_MASK0, 0x00);
	vgsm_outb(card, VGSM_MASK1, 0x00);

	/* Stop DMA */
	vgsm_outb(card, VGSM_DMA_OPER, 0);

	/* Reset Tiger 320  */
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_DMA_RST |
		VGSM_CNTL_SERIAL_RST);

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	/* Free IRQ */
	free_irq(card->pci_dev->irq, card);

	/* Free DMA */
	pci_free_consistent(card->pci_dev, card->writedma_size,
		 card->writedma_mem, card->writedma_bus_mem);

	pci_free_consistent(card->pci_dev, card->readdma_size,
		card->readdma_mem, card->readdma_bus_mem);				
	/* Unmap */
	iounmap(card->io_mem);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_put(card->modules[i]);
		card->modules[i] = NULL;
	}

	vgsm_card_put(card);
}

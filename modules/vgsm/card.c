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
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/bitops.h>

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"
#include "codec.h"

/* Send interrupt to micros - 0x01 for Micro 0, 0x02 for micro 1 */
static inline void vgsm_interrupt_micro(struct vgsm_card *card, 
	u8 value)
{
	vgsm_outb(card, VGSM_PIB_E4, value);
}

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

static void vgsm_write_msg(
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

static inline void vgsm_wait_e0(struct vgsm_card *card)
{
	int i;

	for (i=0; i<100 && vgsm_inb(card, VGSM_PIB_E0); i++) {
		udelay(10);

		if (i > 20)
			printk(KERN_WARNING
				"Uhuh... waiting %d for buffer\n", i);
	}
}

void vgsm_send_msg(
	struct vgsm_card *card,
	int micro,
	struct vgsm_micro_message *msg)
{
	vgsm_wait_e0(card);
	vgsm_write_msg(card, msg);

	if (micro == 0)
		vgsm_interrupt_micro(card, 0x08);
	else if (micro == 1)
		vgsm_interrupt_micro(card, 0x10);
	else
		BUG();
}

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

	vgsm_send_msg(card, 0, &msg);
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

	vgsm_send_msg(card, 0, &msg);
}

void vgsm_send_get_fw_ver(
	struct vgsm_card *card,
	int micro)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_GET_FW_VER;
	msg.numbytes = 0;
	
	vgsm_send_msg(card, micro, &msg);
}
	
void vgsm_update_mask0(struct vgsm_card *card)
{
	int i;

	card->regs.mask0 &= ~(VGSM_INT1STAT_WR_REACH_INT |
				VGSM_INT1STAT_WR_REACH_END |
				VGSM_INT1STAT_RD_REACH_INT |
				VGSM_INT1STAT_RD_REACH_END);

	for(i=0; i<card->num_modules; i++) {

		if (test_bit(VISDN_CHAN_STATE_OPEN,
				&card->modules[i].visdn_chan.state)) {

			// ?

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
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX0, module->tx_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX0, module->rx_gain);

		card->regs.codec_loop &= ~(VGSM_CODEC_LOOPB_AL0 |
			       		VGSM_CODEC_LOOPB_DL0);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL0;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL0;
	break;

	case 1:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX1, module->tx_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX1, module->rx_gain);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL1;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL1;
	break;

	case 2:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX2, module->tx_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX2, module->rx_gain);

		if (module->anal_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_AL2;

		if (module->dig_loop)
			card->regs.codec_loop |= VGSM_CODEC_LOOPB_DL2;
	break;

	case 3:
		vgsm_send_codec_setreg(card, VGSM_CODEC_GTX3, module->tx_gain);
		vgsm_send_codec_setreg(card, VGSM_CODEC_GRX3, module->rx_gain);

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

		module = &card->modules[i];

		if (module->kfifo_rx->size - kfifo_len(module->kfifo_rx) > 8) {
			if (test_and_clear_bit(
					VGSM_MODULE_STATUS_RX_ACK_PENDING,
					&module->status)) {
				vgsm_module_send_ack(module);
			}
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

		module = &card->modules[card->rr_last_module];

		if (kfifo_len(module->kfifo_tx)) {
			if (!test_and_set_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING,
							&module->status)) {

				u8 buf[7];
				int bytes_to_send =
				       	__kfifo_get(module->kfifo_tx, buf, 7);

				wake_up(&module->tx_wait_queue);

				vgsm_module_send_string(module, buf,
					       bytes_to_send);

				module->ack_timeout_timer.expires =
					jiffies + HZ;
				add_timer(&module->ack_timeout_timer);
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
		VGSM_CNTL_MASTER_RST |
		VGSM_CNTL_SERIAL_RST |
		VGSM_CNTL_EXTRST);
	mb();
	msleep(100);
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_MASTER_RST |
		VGSM_CNTL_SERIAL_RST);
	mb();
	msleep(100);
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_DMA_SELF |
		VGSM_CNTL_PIB_CYCLE_3 |
		VGSM_CNTL_EXTRST);
	mb();

	/* Setting serial status registers */
	card->ios_12_status = vgsm_inb(card, VGSM_AUXD);

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
	vgsm_outl(card, VGSM_DMA_WR_INT, cpu_to_le32(card->writedma_bus_mem));
	vgsm_outl(card, VGSM_DMA_WR_END,
		cpu_to_le32(card->writedma_bus_mem + card->writedma_size - 4));

	vgsm_outl(card, VGSM_DMA_RD_START, cpu_to_le32(card->readdma_bus_mem));	
	vgsm_outl(card, VGSM_DMA_RD_INT, cpu_to_le32(card->readdma_bus_mem));	
	vgsm_outl(card, VGSM_DMA_RD_END,
		cpu_to_le32(card->readdma_bus_mem + card->readdma_size - 4));

	/* Clear DMA interrupts */
	vgsm_outb(card, VGSM_INT0STAT, 0x3F);

	/* PIB initialization */
	vgsm_outb(card, VGSM_PIB_E0, 0x00);

	/* Setting polarity control register */
	vgsm_outb(card, VGSM_AUX_POL, 0x03);

	vgsm_msg(KERN_DEBUG, "VGSM card initialized\n");

	return 0;
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

	if (int0stat &
		(VGSM_INT1STAT_WR_REACH_INT |
		VGSM_INT1STAT_WR_REACH_END |
		VGSM_INT1STAT_RD_REACH_INT |
		VGSM_INT1STAT_RD_REACH_END)) {

		/* Read or Write DMA reached interrupt address */
		vgsm_msg(KERN_CRIT, "DMA IRQ\n");

		printk(KERN_CRIT "R: ");

		{
		int j;
		// stampo 32 byte di dati
		for (j=0; j<0x20; j++) {
			printk("%02x", *(volatile u8 *)(card->readdma_mem+j*4));
		}
		printk("\n");
		}

	}

	if (int0stat & VGSM_INT1STAT_PCI_MASTER_ABORT)
		vgsm_msg(KERN_WARNING, "PCI master abort\n");

	if (int0stat & VGSM_INT1STAT_PCI_TARGET_ABORT)
		vgsm_msg(KERN_WARNING, "PCI target abort\n");

	if (int1stat) {
		struct vgsm_micro_message msg;
		struct vgsm_module *module;

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

		if (msg.cmd == VGSM_CMD_S0 || msg.cmd == VGSM_CMD_S1) {
			if (micro == 0) {
				if (msg.cmd == VGSM_CMD_S0)
					module = &card->modules[0];
				else
					module = &card->modules[1];
			} else {
				if (msg.cmd == VGSM_CMD_S0)
					module = &card->modules[2];
				else
					module = &card->modules[3];
			}

#if 0
printk(KERN_CRIT "Mod %d: MSG: %c%c%c%c%c%c%c\n",
	module->id,
	escape_unprintable(msg.payload[0]),
	escape_unprintable(msg.payload[1]),
	escape_unprintable(msg.payload[2]),
	escape_unprintable(msg.payload[3]),
	escape_unprintable(msg.payload[4]),
	escape_unprintable(msg.payload[5]),
	escape_unprintable(msg.payload[6]));
#endif

		/* Got msg from micro, now send ACK */
		
			if (msg.cmd_dep != 0)
				printk(KERN_ERR "cmd_dep != 0 ????\n");

			if (test_bit(VGSM_MODULE_STATUS_RUNNING,
						&module->status))
				__kfifo_put(module->kfifo_rx, msg.payload,
								msg.numbytes);

			set_bit(VGSM_MODULE_STATUS_RX_ACK_PENDING,
				&module->status);

			wake_up(&module->rx_wait_queue);

			tasklet_schedule(&card->rx_tasklet);

		} else if (msg.cmd == VGSM_CMD_MAINT) {
			if (msg.cmd_dep == VGSM_CMD_MAINT_ACK) {
				if (micro == 0) {
					if (msg.numbytes == 0)
						module = &card->modules[0];
					else
						module = &card->modules[1];
				} else {
					if (msg.numbytes == 0)
						module = &card->modules[2];
					else
						module = &card->modules[3];
				}

#if 0
printk(KERN_CRIT "Received ACK from module %d\n\n", module->id);
#endif

			clear_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING,
				&module->status);

			del_timer(&module->ack_timeout_timer);

			tasklet_schedule(&card->tx_tasklet);
		} else if (msg.cmd_dep == VGSM_CMD_MAINT_CODEC_GET) {
			printk(KERN_INFO "CODEC RESP: %02x = %02x\n",
				msg.payload[0], msg.payload[1]);
		} else if (msg.cmd_dep == VGSM_CMD_MAINT_GET_FW_VER) {
			printk(KERN_INFO
				"Micro %d firmware version %c%c%c%c%c%c%c\n",
				micro,
				msg.payload[0],
				msg.payload[1],
				msg.payload[2],
				msg.payload[3],
				msg.payload[4],
				msg.payload[5],
				msg.payload[6]);
		}
	}
	}

err_unexpected_micro:

	return IRQ_HANDLED;
}

static void vgsm_maint_timer(unsigned long data)
{
	struct vgsm_card *card = (struct vgsm_card *)data;

	vgsm_card_lock(card);
//	vgsm_send_codec_getreg(card, VGSM_CODEC_ALARM);
	vgsm_send_codec_getreg(card, VGSM_CODEC_GTX1);
	vgsm_card_unlock(card);

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
	udelay(50);
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

/* Do probing type stuff here */
int vgsm_card_probe(
	struct pci_dev *pci_dev, 
	const struct pci_device_id *ent)
{
	static int numcards;
	int err;
	int i;

	struct vgsm_card *card;
	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		vgsm_msg(KERN_CRIT, "unable to kmalloc!\n");
		err = -ENOMEM;
		goto err_alloc_vgsmcard;
	}

	memset(card, 0, sizeof(*card));

	card->pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, card);

	card->id = numcards++;

	spin_lock_init(&card->lock);

	tasklet_init(&card->rx_tasklet, vgsm_card_rx_tasklet,
			(unsigned long)card);

	tasklet_init(&card->tx_tasklet, vgsm_card_tx_tasklet,
			(unsigned long)card);

	init_timer(&card->maint_timer);
	card->maint_timer.function = vgsm_maint_timer;
	card->maint_timer.data = (unsigned long)card;

	card->num_modules = 4;

	for (i=0; i<card->num_modules; i++)
		vgsm_module_init(&card->modules[i], card, i);

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


	vgsm_msg(KERN_DEBUG, "vgsm card found at 0x%08lx\n", card->io_bus_mem);


	/* Allocate enough DMA memory for 4 modules, receive and transmit.  
	* Each sample written by PCI bridge is 32 bits, 8 bits/module */

	/* READ DMA */
	card->readdma_size = vgsm_DMA_SAMPLES * 4;
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
	card->writedma_size = vgsm_DMA_SAMPLES * 4;
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

	vgsm_initialize_hw(card);

	/* Enable interrupts */
	card->regs.mask0 = 0x00;
	vgsm_outb(card, VGSM_MASK0, card->regs.mask0);
	vgsm_outb(card, VGSM_MASK1, 0x03);

	/* Start DMA */
	vgsm_outb(card, VGSM_OPER, 0x01);

	vgsm_module_send_set_padding_timeout(&card->modules[0], 10);
	vgsm_module_send_set_padding_timeout(&card->modules[1], 10);
	vgsm_module_send_set_padding_timeout(&card->modules[2], 10);
	vgsm_module_send_set_padding_timeout(&card->modules[3], 10);

	vgsm_send_get_fw_ver(card, 0);
	vgsm_send_get_fw_ver(card, 1);

	vgsm_codec_reset(card);

	/* Ensure the modules are turned off */
	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_UNCOND_OFF);
	}

	msleep(200);

	for(i=0; i<card->num_modules; i++)
		vgsm_module_send_onoff(&card->modules[i], 0);

	ssleep(5);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_POWER_ON |
			VGSM_CMD_MAINT_ONOFF_ON);
	}

	msleep(1500);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_POWER_ON);
	}

	for (i=0; i<card->num_modules; i++) {
		err = vgsm_module_register(&card->modules[i], card);
		if (err < 0)
			goto err_module_register;
	}

	spin_lock(&vgsm_cards_list_lock);
	list_add_tail(&card->cards_list_node, &vgsm_cards_list);
	spin_unlock(&vgsm_cards_list_lock);

	card->maint_timer.expires = jiffies + 5 * HZ;
	add_timer(&card->maint_timer);

	return 0;

//	free_irq(pci_dev->irq, card);
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
err_module_register:
	kfree(card);
err_alloc_vgsmcard:

	return err;	
}

void vgsm_card_remove(struct vgsm_card *card)
{
	int i;

	/* Clean up any allocated resources and stuff here */

	vgsm_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n", card->io_mem);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

	del_timer_sync(&card->maint_timer);

	spin_lock(&vgsm_cards_list_lock);
	list_del(&card->cards_list_node);
	spin_unlock(&vgsm_cards_list_lock);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_POWER_ON |
			VGSM_CMD_MAINT_ONOFF_ON);
	}

	msleep(1500);

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_POWER_ON);
	}

	ssleep(5); /* Leave 5s to the modules for shoutdown */

	for(i=0; i<card->num_modules; i++) {
		vgsm_module_send_onoff(&card->modules[i],
			VGSM_CMD_MAINT_ONOFF_UNCOND_OFF);
	}

	msleep(200);

	for(i=0; i<card->num_modules; i++)
		vgsm_module_send_onoff(&card->modules[i], 0);

	/* Disable IRQs */
	vgsm_outb(card, VGSM_MASK0, 0x00);
	vgsm_outb(card, VGSM_MASK1, 0x00);

	/* Reset Tiger 320  */
	vgsm_outb(card, VGSM_CNTL,
		VGSM_CNTL_MASTER_RST |
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

	for(i=0; i<card->num_modules; i++)
		vgsm_module_unregister(&card->modules[i]);

	kfree(card);
}

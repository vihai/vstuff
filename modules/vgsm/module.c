/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
 * Copyright (C) 2006 Daniele Orlandi
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
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/tty.h>

#include <linux/kstreamer/link.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

#include <linux/visdn/port.h>

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"

#ifdef DEBUG_CODE
#define vgsm_debug_module(module, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG vgsm_DRIVER_PREFIX			\
			"%s-%s:"					\
			"module[%s] "					\
			format,						\
			(module)->card->pci_dev->dev.bus->name,		\
			(module)->card->pci_dev->dev.bus_id,		\
			kobject_name(&(module)->ks_node.kobj),	\
			## arg)

#else
#define vgsm_debug_module(module, dbglevel, format, arg...) do {} while (0)
#endif

#define vgsm_msg_module(module, level, format, arg...)			\
	printk(level vgsm_DRIVER_PREFIX					\
		"%s-%s:"						\
		"module[%s] "						\
		format,							\
		(module)->card->pci_dev->dev.bus->name,			\
		(module)->card->pci_dev->dev.bus_id,			\
		kobject_name(&(module)->ks_node.kobj),		\
		## arg)

void vgsm_module_send_set_padding_timeout(
	struct vgsm_module *module,
	u8 timeout)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_TIMEOUT_SET;

	if (module->id == 0 || module->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	msg.payload[0] = timeout;
	
	vgsm_send_msg(module->micro, &msg);
}

void vgsm_module_send_string(
	struct vgsm_module *module,
	u8 *buf,
	int len)
{
	struct vgsm_micro_message msg = { };

	BUG_ON(len > 7);

	if (module->id == 0 || module->id == 2)
		msg.cmd = VGSM_CMD_S0;
	else
		msg.cmd = VGSM_CMD_S1;

	msg.cmd_dep = 0;
	msg.numbytes = len;

	memcpy(msg.payload, buf, len);

	vgsm_send_msg(module->micro, &msg);
}

void vgsm_module_send_power_get(
	struct vgsm_module *module)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_POWER_GET;
	msg.numbytes = 0;
	
	vgsm_send_msg(module->micro, &msg);
}

void vgsm_module_send_ack(
	struct vgsm_module *module)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_ACK;

	if (module->id == 0 || module->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	vgsm_send_msg(module->micro, &msg);
}

void vgsm_module_send_onoff(
	struct vgsm_module *module,
	int onoff_cmd)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_ONOFF;

	if (module->id == 0 || module->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	msg.payload[0] = onoff_cmd;

	vgsm_send_msg(module->micro, &msg);
}

static void vgsm_port_release(
	struct visdn_port *port)
{
	printk(KERN_DEBUG "vgsm_port_release()\n");
}

static int vgsm_port_enable(
	struct visdn_port *visdn_port)
{
	struct vgsm_module *module = port_to_module(visdn_port);
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);


	vgsm_card_unlock(card);

	vgsm_debug_module(module, 2, "enabled\n");

	return 0;
}

static int vgsm_port_disable(
	struct visdn_port *visdn_port)
{
	struct vgsm_module *module = port_to_module(visdn_port);
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);

	vgsm_card_unlock(card);

	vgsm_debug_module(module, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops vgsm_port_ops = {
	.owner		= THIS_MODULE,
	.release	= vgsm_port_release,
	.enable		= vgsm_port_enable,
	.disable	= vgsm_port_disable,
};

static void vgsm_chan_release(struct visdn_chan *chan)
{
	printk(KERN_DEBUG "vgsm_chan_release()\n");

	// FIXME
}

static int vgsm_chan_open(struct visdn_chan *visdn_chan)
{
	struct vgsm_module *module = chan_to_module(visdn_chan);

	vgsm_debug_module(module, 1, "channel opened.\n");

	return 0;
}

static int vgsm_chan_close(struct visdn_chan *visdn_chan)
{
	struct vgsm_module *module = chan_to_module(visdn_chan);

	vgsm_debug_module(module, 1, "channel closed.\n");

	return 0;
}

static int vgsm_chan_start(struct visdn_chan *visdn_chan)
{
	struct vgsm_module *module = chan_to_module(visdn_chan);
	struct vgsm_card *card = module->card;
	int i;

	vgsm_card_lock(card);

	for(i=module->timeslot_offset; i < card->writedma_size; i+=4)
		*(u8 *)(card->writedma_mem + i) = 0x2a;

	module->rx_fifo_pos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
				(u32)card->readdma_bus_mem) / 4;
	module->tx_fifo_pos = ((le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
			(u32)card->writedma_bus_mem + VGSM_FIFO_JITBUFF_AVG) %
			card->writedma_size) / 4;

	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "channel started.\n");

	return 0;
}

static int vgsm_chan_stop(struct visdn_chan *visdn_chan)
{
	struct vgsm_module *module = chan_to_module(visdn_chan);
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);
	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "channel stopped.\n");

	return 0;
}

static int vgsm_chan_leg_connect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
	struct vgsm_module *module = chan_to_module(visdn_leg->chan);

	vgsm_debug_module(module, 2, "leg A connecting to %s\n",
		visdn_leg2->chan->kobj.name);

	return 0;
}

static void vgsm_chan_leg_disconnect(
	struct visdn_leg *visdn_leg,
	struct visdn_leg *visdn_leg2)
{
}

static ssize_t vgsm_chan_leg_read(
	struct visdn_leg *visdn_leg,
	void *buf, size_t count)
{
	struct vgsm_module *module = chan_to_module(visdn_leg->chan);
	struct vgsm_card *card = module->card;
	size_t copied_octets = 0;
	int inpos;
	u8 *bufp = buf;

	vgsm_card_lock(card);

	inpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
				card->readdma_bus_mem) / 4;

	while(module->rx_fifo_pos != inpos && copied_octets < count) {
		*bufp++ = *(u8 *)(card->readdma_mem +
				(module->rx_fifo_pos * 4) +
				module->timeslot_offset);
		module->rx_fifo_pos++;

		if (module->rx_fifo_pos >= module->rx_fifo_size)
			module->rx_fifo_pos = 0;

		copied_octets++;
	}

	vgsm_card_unlock(card);

	return copied_octets;
}

static void vgsm_chan_fifo_write(
	struct vgsm_module *module,
	const u8 *buf,
	size_t count)
{
	struct vgsm_card *card = module->card;
	int i;

	for (i=0; i<count; i++) {
		*(u8 *)(card->writedma_mem + (module->tx_fifo_pos * 4) +
			module->timeslot_offset) = *(buf + i);

		module->tx_fifo_pos++;

		if (module->tx_fifo_pos >= module->tx_fifo_size)
			module->tx_fifo_pos = 0;
	}
}

static ssize_t vgsm_chan_leg_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct vgsm_module *module = chan_to_module(visdn_leg->chan);
	struct vgsm_card *card = module->card;
	size_t copied_octets = 0;
	int outpos;
	int used_octs;

	vgsm_card_lock(card);

	outpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	used_octs = (module->tx_fifo_pos - outpos + module->tx_fifo_size) %
			module->tx_fifo_size;

#if 0
	printk(KERN_DEBUG "U=%d in=%d out=%d count=%d\n",
		used_octs, module->tx_fifo_pos, outpos, count);
#endif

	module->tx_fifo_cycles++;
	if (module->tx_fifo_cycles >= 10) {
		if (module->tx_fifo_min < VGSM_FIFO_JITBUFF_LOW) {
			u8 foo = ((u8 *)buf)[0];
			vgsm_chan_fifo_write(module, &foo, 1);

#if 0
			printk(KERN_DEBUG "TX FIFO under low-mark:"
						" added one sample\n");
#endif
		}

		module->tx_fifo_cycles = 0;
		module->tx_fifo_min = INT_MAX;
		module->tx_fifo_max = 0;
	}

	if (used_octs > VGSM_FIFO_JITBUFF_HIGHDROP && count > 0) {
		module->tx_fifo_pos = outpos + VGSM_FIFO_JITBUFF_LOW;

#if 0
		printk(KERN_DEBUG "FIFO Underrun\n");
#endif

	} else if (used_octs > VGSM_FIFO_JITBUFF_HIGH && count > 0) {
		count--;

#if 0
		printk(KERN_DEBUG "TX FIFO %d over high-mark:"
				" dropped one sample\n",
				used_octs);
#endif
	}

	vgsm_chan_fifo_write(module, buf, count);

	vgsm_card_unlock(card);

	return copied_octets;
}

struct visdn_chan_ops vgsm_chan_ops =
{
	.owner		= THIS_MODULE,

	.release	= vgsm_chan_release,
	.open		= vgsm_chan_open,
	.close		= vgsm_chan_close,
	.start		= vgsm_chan_start,
	.stop		= vgsm_chan_stop,
};

struct visdn_leg_ops vgsm_chan_leg_ops = {
	.owner		= THIS_MODULE,

	.connect	= vgsm_chan_leg_connect,
	.disconnect	= vgsm_chan_leg_disconnect,

	.read		= vgsm_chan_leg_read,
	.write		= vgsm_chan_leg_write,
};

static void vgsm_module_ack_timeout_timer(unsigned long data)
{
	struct vgsm_module *module = (struct vgsm_module *)data;

	printk(KERN_ERR "Timeout waiting for ACK\n");

	kfifo_reset(module->kfifo_tx);
	clear_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING, &module->status);

	wake_up(&module->tx_wait_queue);
	tasklet_schedule(&module->card->tx_tasklet);
}

void vgsm_micro_init(
	struct vgsm_micro *micro,
	struct vgsm_card *card,
	int id)
{
	memset(micro, 0, sizeof(*micro));

	micro->card = card;
	micro->id = id;

	init_completion(&micro->fw_upgrade_ready);
}

void vgsm_module_init(
	struct vgsm_module *module,
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id)
{
	memset(module, 0, sizeof(*module));

	module->micro = micro;
	module->card = card;
	module->id = id;
	module->timeslot_offset = 3 - id;

	init_completion(&module->read_status_completion);

	init_timer(&module->ack_timeout_timer);
	module->ack_timeout_timer.function = vgsm_module_ack_timeout_timer;
	module->ack_timeout_timer.data = (unsigned long)module;

	module->anal_loop = FALSE;
	module->dig_loop = FALSE;

	ks_node_init(&module->ks_node,
			&vgsm_module_node_ops, name,
			&card->pci_dev->dev.kobj);

	vgsm_module_rx_init(&module->rx, module);
	vgsm_module_tx_init(&module->tx, module);
}

struct vgsm_module *vgsm_module_alloc(
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name)
{
	struct vgsm_module *module;

	module = kmalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		goto err_kmalloc;

	vgsm_module_init(module, card, micro, id, name);

	module->tx.fifo = kfifo_alloc(
				vgsm_SERIAL_BUFF, GFP_KERNEL,
				&module->tx.fifo_lock);
	if (IS_ERR(module->tx.fifo))
		goto err_kfifo_tx;

	return module;

	kfifo_free(module->tx.fifo);
err_kfifo_tx:
	kfree(module);
err_kmalloc:

	return NULL;
}

static int vgsm_module_rx_register(struct vgsm_module_rx *module)
{
	int err;

	err = ks_link_register(&module->ks_link);
	if (err < 0)
		goto err_link_register;

	return 0;

	ks_link_unregister(&module->ks_link);
err_link_register:

	return err;
}

static void vgsm_module_rx_unregister(struct vgsm_module_rx *module)
{
	ks_link_unregister(&module->ks_link);
}

static int vgsm_module_tx_register(struct vgsm_module_tx *module)
{
	int err;

	err = ks_link_register(&module->ks_link);
	if (err < 0)
		goto err_link_register;

	return 0;

	ks_link_unregister(&module->ks_link);
err_link_register:

	return err;
}

static void vgsm_module_tx_unregister(struct vgsm_module_tx *module)
{
	ks_link_unregister(&module->ks_link);
}

int vgsm_module_register(struct vgsm_module *module)
{
	int err;

	tty_register_device(vgsm_tty_driver,
			module->card->id * 4 + module->id,
			&module->card->pci_dev->dev);

	/* Node is the object we are inheriting */
	err = ks_node_register(&module->ks_node);
	if (err < 0)
		goto err_node_register;

	vgsm_module_get(module);
	err = vgsm_module_rx_register(&module->rx);
	if (err < 0)
		goto err_st_module_rx_register;

	vgsm_module_get(module);
	err = vgsm_module_tx_register(&module->tx);
	if (err < 0)
		goto err_st_module_tx_register;

	return 0;

	vgsm_module_tx_unregister(&module->tx);
err_st_module_tx_register:
	vgsm_module_rx_unregister(&module->rx);
err_st_module_rx_register:
	ks_node_unregister(&module->ks_node);
err_node_register:

	return err;
}

void vgsm_module_unregister(struct vgsm_module *module)
{
	vgsm_module_tx_unregister(&module->tx);
	vgsm_module_rx_unregister(&module->rx);

	ks_node_unregister(&module->ks_node);

	tty_unregister_device(vgsm_tty_driver,
			module->card->id * 4 + module->id);
}
















































































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

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

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

struct vgsm_module *vgsm_module_get(struct vgsm_module *module)
{
	return vgsm_card_get(module->card) ? module : NULL;
}

void vgsm_module_put(struct vgsm_module *module)
{
	vgsm_card_put(module->card);
}

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

static void vgsm_module_ack_timeout_timer(unsigned long data)
{
	struct vgsm_module *module = (struct vgsm_module *)data;

	printk(KERN_ERR "Timeout waiting for ACK\n");

	kfifo_reset(module->tx.fifo);
	clear_bit(VGSM_MODULE_STATUS_TX_ACK_PENDING, &module->status);

	wake_up(&module->tx.wait_queue);
	tasklet_schedule(&module->card->tx_tasklet);
}

/*------------------------------------------------------------------------*/

static void vgsm_module_rx_chan_release(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;

	vgsm_debug_module(module, 2, "RX released\n");

	vgsm_module_put(module_rx->module);
}

static int vgsm_module_rx_chan_connect(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;

	vgsm_debug_module(module, 2, "RX connected\n");

	return 0;
}

static void vgsm_module_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;

	vgsm_debug_module(module, 2, "RX disconnected\n");
}

static int vgsm_module_rx_chan_open(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;

	vgsm_debug_module(module, 1, "RX opened.\n");

	return 0;
}

static void vgsm_module_rx_chan_close(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;

	vgsm_debug_module(module, 1, "RX closed.\n");
}

static int vgsm_module_rx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);

	module_rx->fifo_pos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
				card->readdma_bus_mem) / 4;

	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "RX started.\n");

	return 0;
}

static void vgsm_module_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);
	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "RX stopped.\n");
}

static void vgsm_module_rx_chan_stimulus(
	struct ks_chan *ks_chan)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);
	struct vgsm_module *module = module_rx->module;
	struct vgsm_card *card = module->card;
	size_t copied_octets = 0;
	int inpos;
	u8 *bufp;

	struct ks_streamframe *sf;

	sf = ks_sf_alloc();
	if (!sf)
		return;

	bufp = sf->data;

	vgsm_card_lock(card);

	inpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
				card->readdma_bus_mem) / 4;

	while(module_rx->fifo_pos != inpos && copied_octets < sf->size) {

		*bufp++ = *(u8 *)(card->readdma_mem +
				(module_rx->fifo_pos * 4) +
				module->timeslot_offset);

		module_rx->fifo_pos++;

		if (module_rx->fifo_pos >= module_rx->fifo_size)
			module_rx->fifo_pos = 0;

		sf->len++;
	}

	vgsm_card_unlock(card);

	vss_chan_push_raw(ks_chan, sf);

	ks_sf_put(sf);
}


static struct ks_chan_ops vgsm_module_rx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vgsm_module_rx_chan_release,
	.connect	= vgsm_module_rx_chan_connect,
	.disconnect	= vgsm_module_rx_chan_disconnect,
	.open		= vgsm_module_rx_chan_open,
	.close		= vgsm_module_rx_chan_close,
	.start		= vgsm_module_rx_chan_start,
	.stop		= vgsm_module_rx_chan_stop,
	.stimulus	= vgsm_module_rx_chan_stimulus,
};


static void vgsm_module_rx_init(
	struct vgsm_module_rx *module_rx,
	struct vgsm_module *module)
{
	module_rx->module = module;

	module_rx->fifo_pos = 0;
	module_rx->fifo_size = module->card->readdma_size / 4;

	module_rx->codec_gain = 0xff;

	ks_chan_init(&module_rx->ks_chan,
			&vgsm_module_rx_chan_ops, "rx",
			NULL,
			&module->ks_node.kobj,
			&module->ks_node,
			&vss_softswitch.ks_node);

/*	module_rx->ks_chan.framed_mtu = -1;
	module_rx->ks_chan.framing_avail = VISDN_LINK_FRAMING_NONE;*/
}

/*----------------------------------------------------------------------------*/

static void vgsm_module_tx_chan_release(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;

	kfifo_free(module_tx->fifo);

	vgsm_module_put(module);
}

static int vgsm_module_tx_chan_connect(struct ks_chan *ks_chan)
{
	return 0;
}

static void vgsm_module_tx_chan_disconnect(struct ks_chan *ks_chan)
{
}

static int vgsm_module_tx_chan_open(struct ks_chan *ks_chan)
{
	return 0;
}

static void vgsm_module_tx_chan_close(struct ks_chan *ks_chan)
{
}

static int vgsm_module_tx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	int i;

	vgsm_card_lock(card);

	/* Fill FIFO with a-law silence */
	for(i=module->timeslot_offset; i < card->writedma_size; i+=4)
		*(u8 *)(card->writedma_mem + i) = 0x2a;

	module_tx->fifo_pos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "TX module started.\n");

	return 0;
}

static void vgsm_module_tx_chan_stop(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;

	vgsm_card_lock(card);
	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "TX module stopped.\n");
}

static void vgsm_module_fifo_write(
	struct vgsm_module_tx *module_tx,
	const u8 *buf,
	size_t count)
{
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	int i;

	for (i=0; i<count; i++) {
		*(u8 *)(card->writedma_mem + (module_tx->fifo_pos * 4) +
			module->timeslot_offset) = *(buf + i);

		module_tx->fifo_pos++;

		if (module_tx->fifo_pos >= module_tx->fifo_size)
			module_tx->fifo_pos = 0;
	}
}

static ssize_t vgsm_module_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	size_t copied_octets = 0;

	vgsm_card_lock(card);

	vgsm_module_fifo_write(module_tx, sf->data, sf->len);

	vgsm_card_unlock(card);

	return copied_octets;
}

static int vgsm_module_tx_chan_get_pressure(
	struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	int outpos;
	int pressure;

	vgsm_card_lock(card);

	outpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	pressure = (module_tx->fifo_pos - outpos + module_tx->fifo_size) %
			module_tx->fifo_size;

	vgsm_card_unlock(card);

	return pressure;
}

static struct ks_chan_ops vgsm_module_tx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vgsm_module_tx_chan_release,
	.connect	= vgsm_module_tx_chan_connect,
	.disconnect	= vgsm_module_tx_chan_disconnect,
	.open		= vgsm_module_tx_chan_open,
	.close		= vgsm_module_tx_chan_close,
	.start		= vgsm_module_tx_chan_start,
	.stop		= vgsm_module_tx_chan_stop,
};

static struct vss_chan_ops vgsm_module_tx_node_ops =
{
	.push_raw	= vgsm_module_tx_chan_push_raw,
	.get_pressure	= vgsm_module_tx_chan_get_pressure,
};

static void vgsm_module_tx_init(
	struct vgsm_module_tx *module_tx,
	struct vgsm_module *module)
{
	module_tx->module = module;

	spin_lock_init(&module_tx->fifo_lock);

	init_waitqueue_head(&module_tx->wait_queue);

	module_tx->fifo_pos = 0;
	module_tx->fifo_size = module->card->writedma_size / 4;
	module_tx->codec_gain = 0xff;

	ks_chan_init(&module_tx->ks_chan,
			&vgsm_module_tx_chan_ops, "tx",
			NULL,
			&module->ks_node.kobj,
			&vss_softswitch.ks_node,
			&module->ks_node);

	module_tx->ks_chan.from_ops = &vgsm_module_tx_node_ops;
/*	module_tx->ks_chan.framed_mtu = -1;
	module_tx->ks_chan.framing_avail = VISDN_LINK_FRAMING_NONE;*/
}

static int vgsm_module_rx_register(struct vgsm_module_rx *module)
{
	int err;

	err = ks_chan_register(&module->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&module->ks_chan);
err_chan_register:

	return err;
}

static void vgsm_module_rx_unregister(struct vgsm_module_rx *module)
{
	ks_chan_unregister(&module->ks_chan);
}

static int vgsm_module_tx_register(struct vgsm_module_tx *module)
{
	int err;

	err = ks_chan_register(&module->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&module->ks_chan);
err_chan_register:

	return err;
}

static void vgsm_module_tx_unregister(struct vgsm_module_tx *module)
{
	ks_chan_unregister(&module->ks_chan);
}




static void vgsm_module_node_release(struct ks_node *node)
{
	struct vgsm_module *module =
		container_of(node, struct vgsm_module, ks_node);

	printk(KERN_DEBUG "vgsm_module_node_release()\n");

	vgsm_module_put(module);
}

static struct ks_node_ops vgsm_module_node_ops = {
	.owner			= THIS_MODULE,

	.release		= vgsm_module_node_release,
};

void vgsm_module_init(
	struct vgsm_module *module,
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name)
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

/*------------------------------------------------------------------------*/

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

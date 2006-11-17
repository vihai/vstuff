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

//	module_rx->fifo_out = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
//				card->readdma_bus_mem) / 4;

//	vgsm_update_mask0(card);
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
//	vgsm_update_mask0(card);
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

//	inpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
//				card->readdma_bus_mem) / 4;

	while(module_rx->fifo_out != inpos && copied_octets < sf->size) {

//		*bufp++ = *(u8 *)(card->readdma_mem +
//				(module_rx->fifo_out * 4) +
//				module->timeslot_offset);

		module_rx->fifo_out++;

		if (module_rx->fifo_out >= module_rx->fifo_size)
			module_rx->fifo_out = 0;

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
	struct vgsm_module *module,
	u16 fifo_base,
	u16 fifo_size)
{
	module_rx->module = module;

	module_rx->fifo_base = fifo_base;
	module_rx->fifo_size = fifo_size;
	module_rx->fifo_out = 0;

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
//	for(i=module->timeslot_offset; i < card->writedma_size; i+=4)
//		*(u8 *)(card->writedma_mem + i) = 0x2a;

//	module_tx->fifo_in = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
//				card->writedma_bus_mem) / 4;

//	vgsm_update_mask0(card);
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
//	vgsm_update_mask0(card);
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
//		*(u8 *)(card->writedma_mem + (module_tx->fifo_in * 4) +
//			module->timeslot_offset) = *(buf + i);

		module_tx->fifo_in++;

		if (module_tx->fifo_in >= module_tx->fifo_size)
			module_tx->fifo_in = 0;
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

//	outpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
//				card->writedma_bus_mem) / 4;

	pressure = (module_tx->fifo_in - outpos + module_tx->fifo_size) %
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
	struct vgsm_module *module,
	u16 fifo_base,
	u16 fifo_size)
{
	module_tx->module = module;

	module_tx->fifo_base = fifo_base;
	module_tx->fifo_size = fifo_size;
	module_tx->fifo_in = 0;

	ks_chan_init(&module_tx->ks_chan,
			&vgsm_module_tx_chan_ops, "tx",
			NULL,
			&module->ks_node.kobj,
			&vss_softswitch.ks_node,
			&module->ks_node);

	module_tx->ks_chan.from_ops = &vgsm_module_tx_node_ops;
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
	int id,
	const char *name,
	u16 rx_fifo_base,
	u16 rx_fifo_size,
	u16 tx_fifo_base,
	u16 tx_fifo_size)
{
	memset(module, 0, sizeof(*module));

	module->card = card;
	module->id = id;

//	init_completion(&module->read_status_completion);

	ks_node_init(&module->ks_node,
			&vgsm_module_node_ops, name,
			&card->pci_dev->dev.kobj);

	memset(&module->asc0, 0, sizeof(module->asc0));
	module->asc0.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	module->asc0.uartclk = 1152000 * 16;
	module->asc0.irq = card->pci_dev->irq;
	module->asc0.dev = &card->pci_dev->dev;

	vgsm_module_rx_init(&module->rx, module, rx_fifo_base, rx_fifo_size);
	vgsm_module_tx_init(&module->tx, module, tx_fifo_base, tx_fifo_size);
}

struct vgsm_module *vgsm_module_alloc(
	struct vgsm_card *card,
	int id,
	const char *name,
	u16 rx_fifo_base,
	u16 rx_fifo_size,
	u16 tx_fifo_base,
	u16 tx_fifo_size)
{
	struct vgsm_module *module;

	module = kmalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		goto err_kmalloc;

	vgsm_module_init(module, card, id, name,
			rx_fifo_base, rx_fifo_size,
			tx_fifo_base, tx_fifo_size);

	return module;

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

/*	module->asc0_line = serial8250_register_port(&module->asc0);
	if (module->asc0_line < 0) {
		printk(KERN_WARNING "Cannot register serial port ASC0: %d\n",
			module->asc0_line);
		err = module->asc0_line;
		goto err_register_asc0;
	}

	module->asc1_line = serial8250_register_port(&module->asc1);
	if (module->asc1_line < 0) {
		printk(KERN_WARNING "Cannot register serial port ASC1: %d\n",
			module->asc1_line);
		err = module->asc1_line;
		goto err_register_asc1;
	}

	module->sim_line = serial8250_register_port(&module->sim);
	if (module->sim_line < 0) {
		printk(KERN_WARNING "Cannot register serial port: %d\n",
			module->sim_line);
		err = module->sim_line;
		goto err_register_sim;
	}

	module->mesim_line = serial8250_register_port(&module->mesim);
	if (module->mesim_line < 0) {
		printk(KERN_WARNING "Cannot register serial port: %d\n",
			module->mesim_line);
		err = module->mesim_line;
		goto err_register_mesim;
	}
*/
	return 0;

	serial8250_unregister_port(module->mesim_line);
err_register_mesim:
	serial8250_unregister_port(module->sim_line);
err_register_sim:
	serial8250_unregister_port(module->asc1_line);
err_register_asc1:
	serial8250_unregister_port(module->asc0_line);
err_register_asc0:
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
/*	serial8250_unregister_port(module->mesim_line);
	serial8250_unregister_port(module->sim_line);
	serial8250_unregister_port(module->asc1_line);
	serial8250_unregister_port(module->asc0_line);*/

	vgsm_module_tx_unregister(&module->tx);
	vgsm_module_rx_unregister(&module->rx);

	ks_node_unregister(&module->ks_node);

	tty_unregister_device(vgsm_tty_driver,
			module->card->id * 4 + module->id);
}

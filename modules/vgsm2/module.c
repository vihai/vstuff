/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
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
	module_rx->fifo_out = vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(module->id));
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
	int inpos;
	u8 *bufp;

	struct ks_streamframe *sf;

	sf = ks_sf_alloc();
	if (!sf)
		return;

	bufp = sf->data;

	vgsm_card_lock(card);

	inpos = vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(module->id));

	while(module_rx->fifo_out != inpos && sf->len < sf->size) {

		if (sf->size - sf->len < sizeof(s16))
			break;

		*(s16 *)(sf->data + sf->len) =
			*(volatile s16 *)(card->fifo_mem +
				module_rx->fifo_base +
				module_rx->fifo_out);

		sf->len += sizeof(s16);

		module_rx->fifo_out += sizeof(s16);

		if (module_rx->fifo_out >= module_rx->fifo_size)
			module_rx->fifo_out = 0;
	}

	vgsm_card_unlock(card);

	kss_chan_push_raw(ks_chan, sf);

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

static struct vgsm_module_rx *vgsm_module_rx_create(
	struct vgsm_module_rx *module_rx,
	struct vgsm_module *module,
	u32 fifo_base,
	u16 fifo_size)
{
	BUG_ON(!module_rx); /* Dynamic allocation not implemented */
	BUG_ON(!module);

	module_rx->module = module;

	module_rx->fifo_base = fifo_base;
	module_rx->fifo_size = fifo_size;
	module_rx->fifo_out = 0;

	ks_chan_create(&module_rx->ks_chan,
			&vgsm_module_rx_chan_ops, "rx",
			NULL,
			&module->ks_node.kobj,
			&module->ks_node,
			&kss_softswitch.ks_node);

	return module_rx;
}

static void vgsm_module_rx_destroy(struct vgsm_module_rx *module_rx)
{
	ks_chan_destroy(&module_rx->ks_chan);
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
//	int i;

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

static ssize_t vgsm_module_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	int i;

	vgsm_card_lock(card);

	for (i=0; i<sf->len; i+=sizeof(s16)) {
		*(volatile s16 *)(card->fifo_mem +
			module_tx->fifo_base +
			module_tx->fifo_in) = *(s16 *)(sf->data + i);

		module_tx->fifo_in += sizeof(s16);

		if (module_tx->fifo_in >= module_tx->fifo_size)
			module_tx->fifo_in = 0;
	}

	vgsm_outl(card, VGSM_R_ME_FIFO_TX_IN(module->id), module_tx->fifo_in);

	vgsm_card_unlock(card);

	return sf->len;
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

	outpos = vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(module->id));

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

static struct kss_chan_from_ops vgsm_module_tx_node_ops =
{
	.push_raw	= vgsm_module_tx_chan_push_raw,
	.get_pressure	= vgsm_module_tx_chan_get_pressure,
};

static struct vgsm_module_tx *vgsm_module_tx_create(
	struct vgsm_module_tx *module_tx,
	struct vgsm_module *module,
	u32 fifo_base,
	u16 fifo_size)
{
	BUG_ON(!module_tx); /* Dynamic allocation not implemented */
	BUG_ON(!module);

	module_tx->module = module;

	module_tx->fifo_base = fifo_base;
	module_tx->fifo_size = fifo_size;
	module_tx->fifo_in = 0;

	ks_chan_create(&module_tx->ks_chan,
			&vgsm_module_tx_chan_ops, "tx",
			NULL,
			&module->ks_node.kobj,
			&kss_softswitch.ks_node,
			&module->ks_node);

	module_tx->ks_chan.from_ops = &vgsm_module_tx_node_ops;

	return module_tx;
}

static void vgsm_module_tx_destroy(struct vgsm_module_tx *module_tx)
{
	ks_chan_destroy(&module_tx->ks_chan);
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

BOOL vgsm_module_power_get(struct vgsm_module *module)
{
	u32 me_status = vgsm_inl(module->card, VGSM_R_ME_STATUS(module->id));

	return !!(me_status & VGSM_R_ME_STATUS_V_VDD);
}

static void vgsm_module_node_release(struct ks_node *node)
{
	struct vgsm_module *module =
		container_of(node, struct vgsm_module, ks_node);

	printk(KERN_DEBUG "vgsm_module_node_release()\n");

	kfree(module);
}

static struct ks_node_ops vgsm_module_node_ops = {
	.owner			= THIS_MODULE,

	.release		= vgsm_module_node_release,
};


static int vgsm_module_ioctl_power_get(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;

	u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(module->id));

	return put_user(me_status & VGSM_R_ME_STATUS_V_VDD, (int __user *)arg);
}

static int vgsm_module_ioctl_power_ign(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;
	u32 old_me_status;

	vgsm_card_lock(module->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(module->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(module->id),
			old_me_status | VGSM_R_ME_SETUP_V_ON);
	vgsm_card_unlock(module->card);

	msleep(100);

	vgsm_card_lock(module->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(module->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(module->id),
			old_me_status & ~VGSM_R_ME_SETUP_V_ON);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_module_ioctl_emerg_off(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;
	u32 old_me_status;

	vgsm_card_lock(module->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(module->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(module->id),
			old_me_status | VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_card_unlock(module->card);

	msleep(3200);

	vgsm_card_lock(module->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(module->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(module->id),
			old_me_status & ~VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_module_ioctl_get_fw_version(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;
	u32 version = vgsm_inl(card, VGSM_R_VERSION);

	return put_user(version, (int __user *)arg);
}

static int vgsm_module_ioctl_sim_route(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;

	if (arg == -1)
		module->route_to_sim = 0xf;
	if (arg == -2)
		module->route_to_sim = module->id;
	if (arg < card->sims_number)
		module->route_to_sim = arg;
	else
		return -EINVAL;

	vgsm_card_update_router(card);

	return 0;
}

static int vgsm_module_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = container_of(uart, struct vgsm_module, asc0);

	switch(cmd) {
	case VGSM_IOC_GET_INTERFACE_VERSION:
		return put_user(2, (int __user *)arg);
	break;

	case VGSM_IOC_GET_NODEID:
		return put_user(module->ks_node.id, (int __user *)arg);
	break;

	case VGSM_IOC_POWER_GET:
		return vgsm_module_ioctl_power_get(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_IGN:
		return vgsm_module_ioctl_power_ign(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_EMERG_OFF:
		return vgsm_module_ioctl_emerg_off(module, cmd, arg);
	break;

	case VGSM_IOC_FW_VERSION:
		return vgsm_module_ioctl_get_fw_version(module, cmd, arg);
	break;

	case VGSM_IOC_SIM_ROUTE:
		return vgsm_module_ioctl_sim_route(module, cmd, arg);
	break;

//	case VGSM_IOC_FW_UPGRADE:
//		return vgsm_tty_do_fw_upgrade(module, cmd, arg);
//	break;
	}

	return -ENOIOCTLCMD;
}



static struct uart_driver vgsm_uart_driver_asc0 =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm_me",
	.dev_name		= "vgsm_me",
	.major			= 0,
	.minor			= 0,
	.nr			= 32,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_asc1 =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm_mea",
	.dev_name		= "vgsm_mea",
	.major			= 0,
	.minor			= 0,
	.nr			= 32,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_mesim =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm_mesim",
	.dev_name		= "vgsm_mesim",
	.major			= 0,
	.minor			= 0,
	.nr			= 32,
	.cons			= NULL,
};

struct vgsm_module *vgsm_module_create(
	struct vgsm_module *module,
	struct vgsm_card *card,
	int id,
	const char *name,
	u32 rx_fifo_base,
	u32 rx_fifo_size,
	u32 tx_fifo_base,
	u32 tx_fifo_size,
	u32 asc0_base,
	u32 asc1_base,
	u32 mesim_base)
{
	BUG_ON(module); /* Static allocation not implemented */

	module = kmalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		goto err_kmalloc;

	memset(module, 0, sizeof(*module));

	module->card = card;
	module->id = id;

//	init_completion(&module->read_status_completion);

	ks_node_create(&module->ks_node,
			&vgsm_module_node_ops, name,
			&card->pci_dev->dev.kobj);

	vgsm_uart_create(&module->asc0,
		&vgsm_uart_driver_asc0,
		module->card->regs_bus_mem + asc0_base,
		module->card->regs_mem + asc0_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * 4 + module->id,
		FALSE,
		vgsm_module_ioctl);

	vgsm_uart_create(&module->asc1,
		&vgsm_uart_driver_asc1,
		module->card->regs_bus_mem + asc1_base,
		module->card->regs_mem + asc1_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * 4 + module->id,
		FALSE,
		NULL);

	vgsm_uart_create(&module->mesim,
		&vgsm_uart_driver_mesim,
		module->card->regs_bus_mem + mesim_base,
		module->card->regs_mem + mesim_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * 4 + module->id,
		TRUE,
		NULL);

	vgsm_module_get(module);
	vgsm_module_rx_create(&module->rx, module, rx_fifo_base, rx_fifo_size);

	vgsm_module_get(module);
	vgsm_module_tx_create(&module->tx, module, tx_fifo_base, tx_fifo_size);

	return module;

	kfree(module);
err_kmalloc:

	return NULL;
}


void vgsm_module_destroy(struct vgsm_module *module)
{
	vgsm_uart_destroy(&module->mesim);
	vgsm_uart_destroy(&module->asc1);
	vgsm_uart_destroy(&module->asc0);

	vgsm_module_tx_destroy(&module->tx);
	vgsm_module_rx_destroy(&module->rx);

	ks_node_destroy(&module->ks_node);
}

/*------------------------------------------------------------------------*/

int vgsm_module_register(struct vgsm_module *module)
{
	int err;

	/* Node is the object we are inheriting */
	err = ks_node_register(&module->ks_node);
	if (err < 0)
		goto err_node_register;

	err = vgsm_module_rx_register(&module->rx);
	if (err < 0)
		goto err_st_module_rx_register;

	err = vgsm_module_tx_register(&module->tx);
	if (err < 0)
		goto err_st_module_tx_register;

	err = vgsm_uart_register(&module->asc0);
	if (err < 0)
		goto err_register_asc0;

	err = vgsm_uart_register(&module->asc1);
	if (err < 0)
		goto err_register_asc1;

	err = vgsm_uart_register(&module->mesim);
	if (err < 0)
		goto err_register_mesim;

	return 0;

	vgsm_uart_unregister(&module->mesim);
err_register_mesim:
	vgsm_uart_unregister(&module->asc1);
err_register_asc1:
	vgsm_uart_unregister(&module->asc0);
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
	vgsm_uart_unregister(&module->mesim);
	vgsm_uart_unregister(&module->asc1);
	vgsm_uart_unregister(&module->asc0);

	vgsm_module_tx_unregister(&module->tx);
	vgsm_module_rx_unregister(&module->rx);

	ks_node_unregister(&module->ks_node);
}

int __init vgsm_module_modinit(void)
{
	int err;

	err = uart_register_driver(&vgsm_uart_driver_asc0);
	if (err < 0)
		goto err_register_driver_asc0;

	err = uart_register_driver(&vgsm_uart_driver_asc1);
	if (err < 0)
		goto err_register_driver_asc1;

	err = uart_register_driver(&vgsm_uart_driver_mesim);
	if (err < 0)
		goto err_register_driver_mesim;

	return 0;

	uart_unregister_driver(&vgsm_uart_driver_mesim);
err_register_driver_mesim:
	uart_unregister_driver(&vgsm_uart_driver_asc1);
err_register_driver_asc1:
	uart_unregister_driver(&vgsm_uart_driver_asc0);
err_register_driver_asc0:

	return err;
}

void __exit vgsm_module_modexit(void)
{
	uart_unregister_driver(&vgsm_uart_driver_mesim);
	uart_unregister_driver(&vgsm_uart_driver_asc1);
	uart_unregister_driver(&vgsm_uart_driver_asc0);
}

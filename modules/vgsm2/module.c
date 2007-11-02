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

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/serial_core.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

#include "vgsm2.h"
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

/*---------------------------------------------------------------------------*/

static ssize_t vgsm_module_identify_show(
	struct ks_node *node,
	struct ks_node_attribute *attr,
	char *buf)
{
	struct vgsm_module *module =
			container_of(node, struct vgsm_module, ks_node);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(VGSM_MODULE_STATUS_IDENTIFY, &module->status) ? 1 : 0);
}

static ssize_t vgsm_module_identify_store(
	struct ks_node *node,
	struct ks_node_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vgsm_module *module =
			container_of(node, struct vgsm_module, ks_node);

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value)
		set_bit(VGSM_MODULE_STATUS_IDENTIFY, &module->status);
	else
		clear_bit(VGSM_MODULE_STATUS_IDENTIFY, &module->status);

	vgsm_led_update();

	return count;
}

static KS_NODE_ATTR(identify, S_IRUGO | S_IWUSR,
		vgsm_module_identify_show,
		vgsm_module_identify_store);

/*---------------------------------------------------------------------------*/

struct vgsm_module *vgsm_module_get(struct vgsm_module *module)
{
	return vgsm_card_get(module->card) ? module : NULL;
}

void vgsm_module_put(struct vgsm_module *module)
{
	vgsm_card_put(module->card);
}

static void vgsm_module_update_fifo_setup(struct vgsm_module *module)
{
	struct vgsm_card *card = module->card;

	vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(module->id),
		(module->rx.compander_enabled ?
			(module->rx.compander_mu_mode ?
				VGSM_R_ME_FIFO_SETUP_V_RX_MULAW :
				VGSM_R_ME_FIFO_SETUP_V_RX_ALAW) :
			VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR) |
		(module->tx.compander_enabled ?
			(module->tx.compander_mu_mode ?
				VGSM_R_ME_FIFO_SETUP_V_TX_MULAW :
				VGSM_R_ME_FIFO_SETUP_V_TX_ALAW) :
			VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR));
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

	module_rx->compander_enabled = FALSE;
	module_rx->compander_mu_mode = FALSE;

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

	vgsm_module_update_fifo_setup(module);

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
	vgsm_module_update_fifo_setup(module);
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
	int sample_size = module_rx->compander_enabled ?
				sizeof(s8) : sizeof(u16);

	struct ks_streamframe *sf;

	sf = ks_sf_alloc();
	if (!sf)
		return;

	bufp = sf->data;

	vgsm_card_lock(card);

	inpos = vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(module->id));

	while(module_rx->fifo_out != inpos && sf->len < sf->size) {

		if (sf->size - sf->len < sample_size)
			break;

		if (sample_size == 1) {
			*(u8 *)(sf->data + sf->len) =
				*(volatile u8 *)(card->fifo_mem +
					module_rx->fifo_base +
					module_rx->fifo_out);
		} else {
			*(s16 *)(sf->data + sf->len) =
				*(volatile s16 *)(card->fifo_mem +
					module_rx->fifo_base +
					module_rx->fifo_out);
		}

		sf->len += sample_size;

		module_rx->fifo_out += sample_size;

		if (module_rx->fifo_out >= module_rx->fifo_size)
			module_rx->fifo_out = 0;
	}

	vgsm_card_unlock(card);

	kss_chan_push_raw(ks_chan, sf);

	ks_sf_put(sf);
}

static int vgsm_module_rx_chan_get_attr_count(struct ks_chan *chan)
{
	return 1;
}

static int vgsm_module_rx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);

	switch(index) {
	case 0: {
		struct ks_amu_compander_descr *descr = buf;

		if (*len < sizeof(*descr))
			return -ENOSPC;

		*type = vgsm_amu_compander_class->id;
		*len = sizeof(*descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = module_rx->compander_enabled;
		descr->mu_mode = module_rx->compander_mu_mode;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vgsm_module_rx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct vgsm_module_rx *module_rx =
		container_of(ks_chan, struct vgsm_module_rx, ks_chan);

	if (type == vgsm_amu_compander_class->id) {
		struct ks_amu_compander_descr *descr = buf;

		if (len < sizeof(*descr))
			return -EINVAL;

		module_rx->compander_enabled = descr->enabled;
		module_rx->compander_mu_mode = descr->mu_mode;
	} else
		return -ENOENT;

	return 0;
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

	.get_attr_count	= vgsm_module_rx_chan_get_attr_count,
	.get_attr	= vgsm_module_rx_chan_get_attr,
	.set_attr	= vgsm_module_rx_chan_set_attr,
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

	vgsm_debug_module(module, 2, "TX released\n");
}

static int vgsm_module_tx_chan_connect(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;

	vgsm_debug_module(module, 2, "TX connected\n");

	module_tx->compander_enabled = FALSE;
	module_tx->compander_mu_mode = FALSE;

	return 0;
}

static void vgsm_module_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;

	vgsm_debug_module(module, 2, "TX disconnected\n");
}

static int vgsm_module_tx_chan_open(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;

	vgsm_debug_module(module, 2, "TX open\n");

	vgsm_module_update_fifo_setup(module);

	return 0;
}

static void vgsm_module_tx_chan_close(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;

	vgsm_debug_module(module, 2, "TX close\n");
}

static int vgsm_module_tx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
//	int i;

	vgsm_card_lock(card);
	vgsm_module_update_fifo_setup(module);

	if (module_tx->compander_enabled) {
		if (module_tx->compander_mu_mode)
			memset(card->fifo_mem + module_tx->fifo_base,
				0xff, module_tx->fifo_size);
		else
			memset(card->fifo_mem + module_tx->fifo_base,
				0x2a, module_tx->fifo_size);
	} else
		memset(card->fifo_mem + module_tx->fifo_base,
			0x0, module_tx->fifo_size);

	module_tx->fifo_in = vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(module->id));

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
	vgsm_card_unlock(card);

	vgsm_debug_module(module, 1, "TX module stopped.\n");
}

static int vgsm_module_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);
	struct vgsm_module *module = module_tx->module;
	struct vgsm_card *card = module->card;
	int i;
	int sample_size = module_tx->compander_enabled ?
				sizeof(u8) : sizeof(s16);

	vgsm_card_lock(card);

	for (i=0; i<sf->len; i += sample_size) {

		if (sample_size == 1) {
			*(volatile u8 *)(card->fifo_mem +
				module_tx->fifo_base +
				module_tx->fifo_in) = *(u8 *)(sf->data + i);
		} else {
			*(volatile s16 *)(card->fifo_mem +
				module_tx->fifo_base +
				module_tx->fifo_in) = *(s16 *)(sf->data + i);
		}

		module_tx->fifo_in += sample_size;

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

static int vgsm_module_tx_chan_get_attr_count(struct ks_chan *chan)
{
	return 1;
}

static int vgsm_module_tx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);

	switch(index) {
	case 0: {
		struct ks_amu_decompander_descr *descr = buf;

		if (*len < sizeof(*descr))
			return -ENOSPC;

		*type = vgsm_amu_decompander_class->id;
		*len = sizeof(*descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = module_tx->compander_enabled;
		descr->mu_mode = module_tx->compander_mu_mode;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vgsm_module_tx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct vgsm_module_tx *module_tx =
		container_of(ks_chan, struct vgsm_module_tx, ks_chan);

	if (type == vgsm_amu_decompander_class->id) {
		struct ks_amu_decompander_descr *descr = buf;

		if (len < sizeof(*descr))
			return -EINVAL;

		module_tx->compander_enabled = descr->enabled;
		module_tx->compander_mu_mode = descr->mu_mode;
	} else
		return -ENOENT;

	return 0;
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

	.get_attr_count	= vgsm_module_tx_chan_get_attr_count,
	.get_attr	= vgsm_module_tx_chan_get_attr,
	.set_attr	= vgsm_module_tx_chan_set_attr,
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

	vgsm_debug_module(module, 1, "vgsm_module_node_release()\n");

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

static int vgsm_module_ioctl_sim_route(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;

	if (arg == -1)
		module->route_to_sim = 0xf;
	else if (arg == -2)
		module->route_to_sim = module->id;
	else if (arg < card->sims_number)
		module->route_to_sim = arg;
	else
		return -EINVAL;

	vgsm_card_update_router(card);

	return 0;
}

static int vgsm_module_ioctl_identify(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	if (arg)
		set_bit(VGSM_MODULE_STATUS_IDENTIFY, &module->status);
	else
		clear_bit(VGSM_MODULE_STATUS_IDENTIFY, &module->status);

	vgsm_led_update();

	return 0;
}

static int vgsm_module_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module =
		container_of(uart, struct vgsm_module, asc0);

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

	case VGSM_IOC_SIM_ROUTE:
		return vgsm_module_ioctl_sim_route(module, cmd, arg);
	break;

	case VGSM_IOC_IDENTIFY:
		return vgsm_module_ioctl_identify(module, cmd, arg);
	break;

	case VGSM_IOC_FW_VERSION: /* Shortcut */
	case VGSM_IOC_READ_SERIAL:
		return vgsm_card_ioctl(module->card, cmd, arg);
	break;
	}


	return -ENOIOCTLCMD;
}

static int vgsm_mesim_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module =
		container_of(uart, struct vgsm_module, mesim);

	switch(cmd) {
	case VGSM_IOC_GET_INTERFACE_VERSION:
		return put_user(2, (int __user *)arg);
	break;

	case VGSM_IOC_SIM_ROUTE:
		return vgsm_module_ioctl_sim_route(module, cmd, arg);
	break;
	}

	return -ENOIOCTLCMD;
}

static struct uart_driver vgsm_uart_driver_asc0 =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm2_me",
	.dev_name		= "vgsm2_me",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MODULES,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_asc1 =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm2_mea",
	.dev_name		= "vgsm2_mea",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MODULES,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_mesim =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm2_mesim",
	.dev_name		= "vgsm2_mesim",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MODULES,
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
		card->id * VGSM_MAX_MODULES + module->id,
		FALSE,
		vgsm_module_ioctl);

	vgsm_uart_create(&module->asc1,
		&vgsm_uart_driver_asc1,
		module->card->regs_bus_mem + asc1_base,
		module->card->regs_mem + asc1_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * VGSM_MAX_MODULES + module->id,
		FALSE,
		NULL);

	vgsm_uart_create(&module->mesim,
		&vgsm_uart_driver_mesim,
		module->card->regs_bus_mem + mesim_base,
		module->card->regs_mem + mesim_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * VGSM_MAX_MODULES + module->id,
		TRUE,
		vgsm_mesim_ioctl);

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

	err = ks_node_create_file(&module->ks_node, &ks_node_attr_identify);
	if (err < 0)
		goto err_create_file;

	return 0;

	ks_node_remove_file(&module->ks_node, &ks_node_attr_identify);
err_create_file:
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
	ks_node_remove_file(&module->ks_node, &ks_node_attr_identify);
	sysfs_remove_link(&module->card->pci_dev->dev.kobj,
			kobject_name(&module->ks_node.kobj));

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

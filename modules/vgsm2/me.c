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
#include "me.h"

#ifdef DEBUG_CODE
#define vgsm_debug_me(me, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG vgsm_DRIVER_PREFIX			\
			"%s-%s:"					\
			"me[%s] "					\
			format,						\
			(me)->card->pci_dev->dev.bus->name,		\
			(me)->card->pci_dev->dev.bus_id,		\
			kobject_name(&(me)->ks_node.kobj),	\
			## arg)

#else
#define vgsm_debug_me(me, dbglevel, format, arg...) do {} while (0)
#endif

#define vgsm_msg_me(me, level, format, arg...)			\
	printk(level vgsm_DRIVER_PREFIX					\
		"%s-%s:"						\
		"me[%s] "						\
		format,							\
		(me)->card->pci_dev->dev.bus->name,			\
		(me)->card->pci_dev->dev.bus_id,			\
		kobject_name(&(me)->ks_node.kobj),		\
		## arg)

/*---------------------------------------------------------------------------*/

static ssize_t vgsm_me_identify_show(
	struct ks_node *node,
	struct ks_node_attribute *attr,
	char *buf)
{
	struct vgsm_me *me =
			container_of(node, struct vgsm_me, ks_node);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(VGSM_ME_STATUS_IDENTIFY, &me->status) ? 1 : 0);
}

static ssize_t vgsm_me_identify_store(
	struct ks_node *node,
	struct ks_node_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vgsm_me *me =
			container_of(node, struct vgsm_me, ks_node);

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value)
		set_bit(VGSM_ME_STATUS_IDENTIFY, &me->status);
	else
		clear_bit(VGSM_ME_STATUS_IDENTIFY, &me->status);

	vgsm_led_update();

	return count;
}

static KS_NODE_ATTR(identify, S_IRUGO | S_IWUSR,
		vgsm_me_identify_show,
		vgsm_me_identify_store);

/*---------------------------------------------------------------------------*/

struct vgsm_me *vgsm_me_get(struct vgsm_me *me)
{
	return vgsm_card_get(me->card) ? me : NULL;
}

void vgsm_me_put(struct vgsm_me *me)
{
	vgsm_card_put(me->card);
}

static void vgsm_me_update_fifo_setup(struct vgsm_me *me)
{
	struct vgsm_card *card = me->card;

	vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(me->id),
		(me->rx.compander_enabled ?
			(me->rx.compander_mu_mode ?
				VGSM_R_ME_FIFO_SETUP_V_RX_MULAW :
				VGSM_R_ME_FIFO_SETUP_V_RX_ALAW) :
			VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR) |
		(me->tx.compander_enabled ?
			(me->tx.compander_mu_mode ?
				VGSM_R_ME_FIFO_SETUP_V_TX_MULAW :
				VGSM_R_ME_FIFO_SETUP_V_TX_ALAW) :
			VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR));
}

/*------------------------------------------------------------------------*/

static void vgsm_me_rx_chan_release(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;

	vgsm_debug_me(me, 2, "RX released\n");

	vgsm_me_put(me_rx->me);
}

static int vgsm_me_rx_chan_connect(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;

	vgsm_debug_me(me, 2, "RX connected\n");

	me_rx->compander_enabled = FALSE;
	me_rx->compander_mu_mode = FALSE;

	return 0;
}

static void vgsm_me_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;

	vgsm_debug_me(me, 2, "RX disconnected\n");
}

static int vgsm_me_rx_chan_open(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;

	vgsm_debug_me(me, 1, "RX opened.\n");

	vgsm_me_update_fifo_setup(me);

	return 0;
}

static void vgsm_me_rx_chan_close(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;

	vgsm_debug_me(me, 1, "RX closed.\n");
}

static int vgsm_me_rx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;
	struct vgsm_card *card = me->card;

	vgsm_card_lock(card);
	vgsm_me_update_fifo_setup(me);
	me_rx->fifo_out = vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(me->id));
	vgsm_card_unlock(card);

	vgsm_debug_me(me, 1, "RX started.\n");

	return 0;
}

static void vgsm_me_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;
	struct vgsm_card *card = me->card;

	vgsm_card_lock(card);
	vgsm_card_unlock(card);

	vgsm_debug_me(me, 1, "RX stopped.\n");
}

static void vgsm_me_rx_chan_stimulus(
	struct ks_chan *ks_chan)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);
	struct vgsm_me *me = me_rx->me;
	struct vgsm_card *card = me->card;
	int inpos;
	u8 *bufp;
	int sample_size = me_rx->compander_enabled ?
				sizeof(s8) : sizeof(u16);

	struct ks_streamframe *sf;

	sf = ks_sf_alloc();
	if (!sf)
		return;

	bufp = sf->data;

	vgsm_card_lock(card);

	inpos = vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(me->id));

	while(me_rx->fifo_out != inpos && sf->len < sf->size) {

		if (sf->size - sf->len < sample_size)
			break;

		if (sample_size == 1) {
			*(u8 *)(sf->data + sf->len) =
				*(volatile u8 *)(card->fifo_mem +
					me_rx->fifo_base +
					me_rx->fifo_out);
		} else {
			*(s16 *)(sf->data + sf->len) =
				*(volatile s16 *)(card->fifo_mem +
					me_rx->fifo_base +
					me_rx->fifo_out);
		}

		sf->len += sample_size;

		me_rx->fifo_out += sample_size;

		if (me_rx->fifo_out >= me_rx->fifo_size)
			me_rx->fifo_out = 0;
	}

	vgsm_card_unlock(card);

	kss_chan_push_raw(ks_chan, sf);

	ks_sf_put(sf);
}

static int vgsm_me_rx_chan_get_attr_count(struct ks_chan *chan)
{
	return 1;
}

static int vgsm_me_rx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);

	switch(index) {
	case 0: {
		struct ks_amu_compander_descr *descr = buf;

		if (*len < sizeof(*descr))
			return -ENOSPC;

		*type = vgsm_amu_compander_class->id;
		*len = sizeof(*descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = me_rx->compander_enabled;
		descr->mu_mode = me_rx->compander_mu_mode;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vgsm_me_rx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct vgsm_me_rx *me_rx =
		container_of(ks_chan, struct vgsm_me_rx, ks_chan);

	if (type == vgsm_amu_compander_class->id) {
		struct ks_amu_compander_descr *descr = buf;

		if (len < sizeof(*descr))
			return -EINVAL;

		me_rx->compander_enabled = descr->enabled;
		me_rx->compander_mu_mode = descr->mu_mode;
	} else
		return -ENOENT;

	return 0;
}

static struct ks_chan_ops vgsm_me_rx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vgsm_me_rx_chan_release,
	.connect	= vgsm_me_rx_chan_connect,
	.disconnect	= vgsm_me_rx_chan_disconnect,
	.open		= vgsm_me_rx_chan_open,
	.close		= vgsm_me_rx_chan_close,
	.start		= vgsm_me_rx_chan_start,
	.stop		= vgsm_me_rx_chan_stop,
	.stimulus	= vgsm_me_rx_chan_stimulus,

	.get_attr_count	= vgsm_me_rx_chan_get_attr_count,
	.get_attr	= vgsm_me_rx_chan_get_attr,
	.set_attr	= vgsm_me_rx_chan_set_attr,
};

static struct vgsm_me_rx *vgsm_me_rx_create(
	struct vgsm_me_rx *me_rx,
	struct vgsm_me *me,
	u32 fifo_base,
	u16 fifo_size)
{
	BUG_ON(!me_rx); /* Dynamic allocation not implemented */
	BUG_ON(!me);

	me_rx->me = me;

	me_rx->fifo_base = fifo_base;
	me_rx->fifo_size = fifo_size;
	me_rx->fifo_out = 0;

	ks_chan_create(&me_rx->ks_chan,
			&vgsm_me_rx_chan_ops, "rx",
			NULL,
			&me->ks_node.kobj,
			&me->ks_node,
			&kss_softswitch.ks_node);

	return me_rx;
}

static void vgsm_me_rx_destroy(struct vgsm_me_rx *me_rx)
{
	ks_chan_destroy(&me_rx->ks_chan);
}

/*----------------------------------------------------------------------------*/

static void vgsm_me_tx_chan_release(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	vgsm_debug_me(me, 2, "TX released\n");
}

static int vgsm_me_tx_chan_connect(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	vgsm_debug_me(me, 2, "TX connected\n");

	me_tx->compander_enabled = FALSE;
	me_tx->compander_mu_mode = FALSE;

	return 0;
}

static void vgsm_me_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	vgsm_debug_me(me, 2, "TX disconnected\n");
}

static int vgsm_me_tx_chan_open(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	vgsm_debug_me(me, 2, "TX open\n");

	vgsm_me_update_fifo_setup(me);

	return 0;
}

static void vgsm_me_tx_chan_close(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	vgsm_debug_me(me, 2, "TX close\n");
}

static int vgsm_me_tx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
//	int i;

	vgsm_card_lock(card);
	vgsm_me_update_fifo_setup(me);

	if (me_tx->compander_enabled) {
		if (me_tx->compander_mu_mode)
			memset(card->fifo_mem + me_tx->fifo_base,
				0xff, me_tx->fifo_size);
		else
			memset(card->fifo_mem + me_tx->fifo_base,
				0x2a, me_tx->fifo_size);
	} else
		memset(card->fifo_mem + me_tx->fifo_base,
			0x0, me_tx->fifo_size);

	me_tx->fifo_in = vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(me->id));

	vgsm_card_unlock(card);

	vgsm_debug_me(me, 1, "TX me started.\n");

	return 0;
}

static void vgsm_me_tx_chan_stop(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;

	vgsm_card_lock(card);
	vgsm_card_unlock(card);

	vgsm_debug_me(me, 1, "TX me stopped.\n");
}

static int vgsm_me_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	int i;
	int sample_size = me_tx->compander_enabled ?
				sizeof(u8) : sizeof(s16);

	vgsm_card_lock(card);

	for (i=0; i<sf->len; i += sample_size) {

		if (sample_size == 1) {
			*(volatile u8 *)(card->fifo_mem +
				me_tx->fifo_base +
				me_tx->fifo_in) = *(u8 *)(sf->data + i);
		} else {
			*(volatile s16 *)(card->fifo_mem +
				me_tx->fifo_base +
				me_tx->fifo_in) = *(s16 *)(sf->data + i);
		}

		me_tx->fifo_in += sample_size;

		if (me_tx->fifo_in >= me_tx->fifo_size)
			me_tx->fifo_in = 0;
	}

	vgsm_outl(card, VGSM_R_ME_FIFO_TX_IN(me->id), me_tx->fifo_in);

	vgsm_card_unlock(card);

	return sf->len;
}

static int vgsm_me_tx_chan_get_pressure(
	struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	int outpos;
	int pressure;

	vgsm_card_lock(card);

	outpos = vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(me->id));

	pressure = (me_tx->fifo_in - outpos + me_tx->fifo_size) %
			me_tx->fifo_size;

	vgsm_card_unlock(card);

	return pressure;
}

static int vgsm_me_tx_chan_get_attr_count(struct ks_chan *chan)
{
	return 1;
}

static int vgsm_me_tx_chan_get_attr(
	struct ks_chan *ks_chan,
	int index,
	__u16 *type,
	void *buf,
	int *len)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);

	switch(index) {
	case 0: {
		struct ks_amu_decompander_descr *descr = buf;

		if (*len < sizeof(*descr))
			return -ENOSPC;

		*type = vgsm_amu_decompander_class->id;
		*len = sizeof(*descr);

		memset(descr, 0, sizeof(*descr));
		descr->hardware = 1;
		descr->enabled = me_tx->compander_enabled;
		descr->mu_mode = me_tx->compander_mu_mode;
	}
	break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vgsm_me_tx_chan_set_attr(
	struct ks_chan *ks_chan,
	__u16 type,
	void *buf,
	int len)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);

	if (type == vgsm_amu_decompander_class->id) {
		struct ks_amu_decompander_descr *descr = buf;

		if (len < sizeof(*descr))
			return -EINVAL;

		me_tx->compander_enabled = descr->enabled;
		me_tx->compander_mu_mode = descr->mu_mode;
	} else
		return -ENOENT;

	return 0;
}

static struct ks_chan_ops vgsm_me_tx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vgsm_me_tx_chan_release,
	.connect	= vgsm_me_tx_chan_connect,
	.disconnect	= vgsm_me_tx_chan_disconnect,
	.open		= vgsm_me_tx_chan_open,
	.close		= vgsm_me_tx_chan_close,
	.start		= vgsm_me_tx_chan_start,
	.stop		= vgsm_me_tx_chan_stop,

	.get_attr_count	= vgsm_me_tx_chan_get_attr_count,
	.get_attr	= vgsm_me_tx_chan_get_attr,
	.set_attr	= vgsm_me_tx_chan_set_attr,
};

static struct kss_chan_from_ops vgsm_me_tx_node_ops =
{
	.push_raw	= vgsm_me_tx_chan_push_raw,
	.get_pressure	= vgsm_me_tx_chan_get_pressure,
};

static struct vgsm_me_tx *vgsm_me_tx_create(
	struct vgsm_me_tx *me_tx,
	struct vgsm_me *me,
	u32 fifo_base,
	u16 fifo_size)
{
	BUG_ON(!me_tx); /* Dynamic allocation not implemented */
	BUG_ON(!me);

	me_tx->me = me;

	me_tx->fifo_base = fifo_base;
	me_tx->fifo_size = fifo_size;
	me_tx->fifo_in = 0;

	ks_chan_create(&me_tx->ks_chan,
			&vgsm_me_tx_chan_ops, "tx",
			NULL,
			&me->ks_node.kobj,
			&kss_softswitch.ks_node,
			&me->ks_node);

	me_tx->ks_chan.from_ops = &vgsm_me_tx_node_ops;

	return me_tx;
}

static void vgsm_me_tx_destroy(struct vgsm_me_tx *me_tx)
{
	ks_chan_destroy(&me_tx->ks_chan);
}

static int vgsm_me_rx_register(struct vgsm_me_rx *me)
{
	int err;

	err = ks_chan_register(&me->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&me->ks_chan);
err_chan_register:

	return err;
}

static void vgsm_me_rx_unregister(struct vgsm_me_rx *me)
{
	ks_chan_unregister(&me->ks_chan);
}

static int vgsm_me_tx_register(struct vgsm_me_tx *me)
{
	int err;

	err = ks_chan_register(&me->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&me->ks_chan);
err_chan_register:

	return err;
}

static void vgsm_me_tx_unregister(struct vgsm_me_tx *me)
{
	ks_chan_unregister(&me->ks_chan);
}

BOOL vgsm_me_power_get(struct vgsm_me *me)
{
	u32 me_status = vgsm_inl(me->card, VGSM_R_ME_STATUS(me->id));

	return !!(me_status & VGSM_R_ME_STATUS_V_VDD);
}

static void vgsm_me_node_release(struct ks_node *node)
{
	struct vgsm_me *me =
		container_of(node, struct vgsm_me, ks_node);

	vgsm_debug_me(me, 1, "vgsm_me_node_release()\n");

	kfree(me);
}

static struct ks_node_ops vgsm_me_node_ops = {
	.owner			= THIS_MODULE,

	.release		= vgsm_me_node_release,
};


static int vgsm_me_ioctl_power_get(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = me->card;

	u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(me->id));

	return put_user(me_status & VGSM_R_ME_STATUS_V_VDD, (int __user *)arg);
}

static int vgsm_me_ioctl_power_ign(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = me->card;
	u32 old_me_status;

	vgsm_card_lock(me->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(me->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(me->id),
			old_me_status | VGSM_R_ME_SETUP_V_ON);
	vgsm_card_unlock(me->card);

	msleep(100);

	vgsm_card_lock(me->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(me->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(me->id),
			old_me_status & ~VGSM_R_ME_SETUP_V_ON);
	vgsm_card_unlock(me->card);

	return 0;
}

static int vgsm_me_ioctl_emerg_off(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = me->card;
	u32 old_me_status;

	vgsm_card_lock(me->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(me->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(me->id),
			old_me_status | VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_card_unlock(me->card);

	msleep(3200);

	vgsm_card_lock(me->card);
	old_me_status = vgsm_inl(card, VGSM_R_ME_SETUP(me->id));
	vgsm_outl(card, VGSM_R_ME_SETUP(me->id),
			old_me_status & ~VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_card_unlock(me->card);

	return 0;
}

static int vgsm_me_ioctl_get_sim_route(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long arg)
{
	if (me->route_to_sim == 0xf)
		put_user(VGSM_SIM_ROUTE_EXTERNAL, (int __user *)arg);
	else
		put_user(me->route_to_sim, (int __user *)arg);

	return 0;
}

static int vgsm_me_ioctl_set_sim_route(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long argul)
{
	struct vgsm_card *card = me->card;
	int arg = (int)argul;

	if (arg == VGSM_SIM_ROUTE_EXTERNAL)
		me->route_to_sim = 0xf;
	else if (arg == VGSM_SIM_ROUTE_DEFAULT)
		me->route_to_sim = me->id;
	else if (arg < card->sims_number)
		me->route_to_sim = arg;
	else
		return -EINVAL;

	vgsm_card_update_router(card);

	return 0;
}

static int vgsm_me_ioctl_identify(
	struct vgsm_me *me,
	unsigned int cmd,
	unsigned long arg)
{
	if (arg)
		set_bit(VGSM_ME_STATUS_IDENTIFY, &me->status);
	else
		clear_bit(VGSM_ME_STATUS_IDENTIFY, &me->status);

	vgsm_led_update();

	return 0;
}

static int vgsm_me_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_me *me =
		container_of(uart, struct vgsm_me, asc0);

	switch(cmd) {
	case VGSM_IOC_GET_INTERFACE_VERSION:
		return put_user(2, (int __user *)arg);

	case VGSM_IOC_GET_NODEID:
		return put_user(me->ks_node.id, (int __user *)arg);

	case VGSM_IOC_POWER_GET:
		return vgsm_me_ioctl_power_get(me, cmd, arg);

	case VGSM_IOC_POWER_IGN:
		return vgsm_me_ioctl_power_ign(me, cmd, arg);

	case VGSM_IOC_POWER_EMERG_OFF:
		return vgsm_me_ioctl_emerg_off(me, cmd, arg);

	case VGSM_IOC_GET_SIM_ROUTE:
		return vgsm_me_ioctl_get_sim_route(me, cmd, arg);

	case VGSM_IOC_SET_SIM_ROUTE:
		return vgsm_me_ioctl_set_sim_route(me, cmd, arg);

	case VGSM_IOC_IDENTIFY:
		return vgsm_me_ioctl_identify(me, cmd, arg);

	case VGSM_IOC_FW_VERSION: /* Shortcut */
	case VGSM_IOC_READ_SERIAL:
	case VGSM_IOC_CARD_GET_ID:
		return vgsm_card_ioctl(me->card, cmd, arg);
	}


	return -ENOIOCTLCMD;
}

static int vgsm_mesim_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_me *me =
		container_of(uart, struct vgsm_me, mesim);

	switch(cmd) {
	case VGSM_IOC_GET_INTERFACE_VERSION:
		return put_user(2, (int __user *)arg);

	case VGSM_IOC_GET_SIM_ROUTE:
		return vgsm_me_ioctl_get_sim_route(me, cmd, arg);

	case VGSM_IOC_SET_SIM_ROUTE:
		return vgsm_me_ioctl_set_sim_route(me, cmd, arg);
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
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MES,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_asc1 =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm2_mea",
	.dev_name		= "vgsm2_mea",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MES,
	.cons			= NULL,
};

static struct uart_driver vgsm_uart_driver_mesim =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm2_mesim",
	.dev_name		= "vgsm2_mesim",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MES,
	.cons			= NULL,
};

struct vgsm_me *vgsm_me_create(
	struct vgsm_me *me,
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
	BUG_ON(me); /* Static allocation not implemented */

	me = kmalloc(sizeof(*me), GFP_KERNEL);
	if (!me)
		goto err_kmalloc;

	memset(me, 0, sizeof(*me));

	me->card = card;
	me->id = id;
	me->route_to_sim = id;

//	init_completion(&me->read_status_completion);

	ks_node_create(&me->ks_node,
			&vgsm_me_node_ops, name,
			&card->pci_dev->dev.kobj);

	vgsm_uart_create(&me->asc0,
		&vgsm_uart_driver_asc0,
		me->card->regs_bus_mem + asc0_base,
		me->card->regs_mem + asc0_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * VGSM_MAX_MES + me->id,
		vgsm_me_ioctl);

	vgsm_uart_create(&me->asc1,
		&vgsm_uart_driver_asc1,
		me->card->regs_bus_mem + asc1_base,
		me->card->regs_mem + asc1_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * VGSM_MAX_MES + me->id,
		NULL);

	vgsm_uart_create(&me->mesim,
		&vgsm_uart_driver_mesim,
		me->card->regs_bus_mem + mesim_base,
		me->card->regs_mem + mesim_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * VGSM_MAX_MES + me->id,
		vgsm_mesim_ioctl);

	vgsm_me_get(me);
	vgsm_me_rx_create(&me->rx, me, rx_fifo_base, rx_fifo_size);

	vgsm_me_get(me);
	vgsm_me_tx_create(&me->tx, me, tx_fifo_base, tx_fifo_size);

	return me;

	kfree(me);
err_kmalloc:

	return NULL;
}


void vgsm_me_destroy(struct vgsm_me *me)
{
	vgsm_uart_destroy(&me->mesim);
	vgsm_uart_destroy(&me->asc1);
	vgsm_uart_destroy(&me->asc0);

	vgsm_me_tx_destroy(&me->tx);
	vgsm_me_rx_destroy(&me->rx);

	ks_node_destroy(&me->ks_node);
}

/*------------------------------------------------------------------------*/

int vgsm_me_register(struct vgsm_me *me)
{
	int err;

	/* Node is the object we are inheriting */
	err = ks_node_register(&me->ks_node);
	if (err < 0)
		goto err_node_register;

	err = vgsm_me_rx_register(&me->rx);
	if (err < 0)
		goto err_st_me_rx_register;

	err = vgsm_me_tx_register(&me->tx);
	if (err < 0)
		goto err_st_me_tx_register;

	err = vgsm_uart_register(&me->asc0);
	if (err < 0)
		goto err_register_asc0;

	err = vgsm_uart_register(&me->asc1);
	if (err < 0)
		goto err_register_asc1;

	err = vgsm_uart_register(&me->mesim);
	if (err < 0)
		goto err_register_mesim;

	err = ks_node_create_file(&me->ks_node, &ks_node_attr_identify);
	if (err < 0)
		goto err_create_file;

	return 0;

	ks_node_remove_file(&me->ks_node, &ks_node_attr_identify);
err_create_file:
	vgsm_uart_unregister(&me->mesim);
err_register_mesim:
	vgsm_uart_unregister(&me->asc1);
err_register_asc1:
	vgsm_uart_unregister(&me->asc0);
err_register_asc0:
	vgsm_me_tx_unregister(&me->tx);
err_st_me_tx_register:
	vgsm_me_rx_unregister(&me->rx);
err_st_me_rx_register:
	ks_node_unregister(&me->ks_node);
err_node_register:

	return err;
}

void vgsm_me_unregister(struct vgsm_me *me)
{
	ks_node_remove_file(&me->ks_node, &ks_node_attr_identify);

	vgsm_uart_unregister(&me->mesim);
	vgsm_uart_unregister(&me->asc1);
	vgsm_uart_unregister(&me->asc0);

	vgsm_me_tx_unregister(&me->tx);
	vgsm_me_rx_unregister(&me->rx);

	ks_node_unregister(&me->ks_node);
}

int __init vgsm_me_modinit(void)
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

void __exit vgsm_me_modexit(void)
{
	uart_unregister_driver(&vgsm_uart_driver_mesim);
	uart_unregister_driver(&vgsm_uart_driver_asc1);
	uart_unregister_driver(&vgsm_uart_driver_asc0);
}

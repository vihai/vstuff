/*
 * VoiSmart vGSM-I card driver
 *
 * Copyright (C) 2005-2007 Daniele Orlandi
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

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

#include "vgsm.h"
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

struct vgsm_me *vgsm_me_get(struct vgsm_me *me)
{
	return vgsm_card_get(me->card) ? me : NULL;
}

void vgsm_me_put(struct vgsm_me *me)
{
	vgsm_card_put(me->card);
}

void vgsm_me_send_set_padding_timeout(
	struct vgsm_me *me,
	u8 timeout)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_TIMEOUT_SET;

	if (me->id == 0 || me->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	msg.payload[0] = timeout;

	vgsm_send_msg(me->micro, &msg);
}

void vgsm_me_send_string(
	struct vgsm_me *me,
	u8 *buf,
	int len)
{
	struct vgsm_micro_message msg = { };

	BUG_ON(len > 7);

	if (me->id == 0 || me->id == 2)
		msg.cmd = VGSM_CMD_S0;
	else
		msg.cmd = VGSM_CMD_S1;

	msg.cmd_dep = 0;
	msg.numbytes = len;

	memcpy(msg.payload, buf, len);

	vgsm_send_msg(me->micro, &msg);
}

void vgsm_me_send_power_get(
	struct vgsm_me *me)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_POWER_GET;
	msg.numbytes = 0;

	vgsm_send_msg(me->micro, &msg);
}

void vgsm_me_send_ack(
	struct vgsm_me *me)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_ACK;

	if (me->id == 0 || me->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	vgsm_send_msg(me->micro, &msg);
}

void vgsm_me_send_onoff(
	struct vgsm_me *me,
	int onoff_cmd)
{
	struct vgsm_micro_message msg = { };

	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_ONOFF;

	if (me->id == 0 || me->id == 2)
		msg.numbytes = 0;
	else
		msg.numbytes = 1;

	msg.payload[0] = onoff_cmd;

	vgsm_send_msg(me->micro, &msg);
}

static void vgsm_me_ack_timeout_timer(unsigned long data)
{
	struct vgsm_me *me = (struct vgsm_me *)data;

	printk(KERN_ERR "Timeout waiting for ACK\n");

	kfifo_reset(me->tx.fifo);
	clear_bit(VGSM_ME_STATUS_TX_ACK_PENDING, &me->status);

	wake_up(&me->tx.wait_queue);
	tasklet_schedule(&me->card->tx_tasklet);
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

	me_rx->fifo_pos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_RD_CUR)) -
				card->readdma_bus_mem) / 4;

	vgsm_update_mask0(card);
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
	vgsm_update_mask0(card);
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

	while(me_rx->fifo_pos != inpos && copied_octets < sf->size) {

		*bufp++ = *(u8 *)(card->readdma_mem +
				(me_rx->fifo_pos * 4) +
				me->timeslot_offset);

		me_rx->fifo_pos++;

		if (me_rx->fifo_pos >= me_rx->fifo_size)
			me_rx->fifo_pos = 0;

		sf->len++;
	}

	vgsm_card_unlock(card);

	kss_chan_push_raw(ks_chan, sf);

	ks_sf_put(sf);
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
};


static struct vgsm_me_rx *vgsm_me_rx_create(
	struct vgsm_me_rx *me_rx,
	struct vgsm_me *me)
{
	BUG_ON(!me_rx); /* Dynamic allocation not implemented */
	BUG_ON(!me);

	me_rx->me = me;

	me_rx->fifo_pos = 0;
	me_rx->fifo_size = me->card->readdma_size / 4;

	me_rx->codec_gain = 0xff;

	ks_chan_create(&me_rx->ks_chan,
			&vgsm_me_rx_chan_ops, "rx",
			NULL,
			&me->ks_node.kobj,
			&me->ks_node,
			&kss_softswitch.ks_node);

	return me_rx;
}

/*----------------------------------------------------------------------------*/

static void vgsm_me_tx_chan_release(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;

	kfifo_free(me_tx->fifo);

	vgsm_me_put(me);
}

static int vgsm_me_tx_chan_connect(struct ks_chan *ks_chan)
{
	return 0;
}

static void vgsm_me_tx_chan_disconnect(struct ks_chan *ks_chan)
{
}

static int vgsm_me_tx_chan_open(struct ks_chan *ks_chan)
{
	return 0;
}

static void vgsm_me_tx_chan_close(struct ks_chan *ks_chan)
{
}

static int vgsm_me_tx_chan_start(struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	int i;

	vgsm_card_lock(card);

	/* Fill FIFO with a-law silence */
	for(i=me->timeslot_offset; i < card->writedma_size; i+=4)
		*(u8 *)(card->writedma_mem + i) = 0x2a;

	me_tx->fifo_pos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	vgsm_update_mask0(card);
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
	vgsm_update_mask0(card);
	vgsm_card_unlock(card);

	vgsm_debug_me(me, 1, "TX me stopped.\n");
}

static void vgsm_me_fifo_write(
	struct vgsm_me_tx *me_tx,
	const u8 *buf,
	size_t count)
{
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	u32 outpos;
	int i;

	outpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	if (me_tx->fifo_underrun ||
	    ((me_tx->fifo_pos - outpos + me_tx->fifo_size) %
	    me_tx->fifo_size) > me_tx->fifo_size / 2) {
		me_tx->fifo_pos = outpos;
		me_tx->fifo_underrun = FALSE;
	}

	for (i=0; i<count; i++) {
		*(u8 *)(card->writedma_mem + (me_tx->fifo_pos * 4) +
			me->timeslot_offset) = *(buf + i);

		me_tx->fifo_pos++;

		if (me_tx->fifo_pos >= me_tx->fifo_size)
			me_tx->fifo_pos = 0;
	}
}

static int vgsm_me_tx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	size_t copied_octets = 0;

	vgsm_card_lock(card);

	vgsm_me_fifo_write(me_tx, sf->data, sf->len);

	vgsm_card_unlock(card);

	return copied_octets;
}

static int vgsm_me_tx_chan_get_pressure(
	struct ks_chan *ks_chan)
{
	struct vgsm_me_tx *me_tx =
		container_of(ks_chan, struct vgsm_me_tx, ks_chan);
	struct vgsm_me *me = me_tx->me;
	struct vgsm_card *card = me->card;
	u32 outpos;
	int pressure;

	vgsm_card_lock(card);

	outpos = (le32_to_cpu(vgsm_inl(card, VGSM_DMA_WR_CUR)) -
				card->writedma_bus_mem) / 4;

	pressure = (me_tx->fifo_pos - outpos + me_tx->fifo_size) %
			me_tx->fifo_size;

	if (me_tx->fifo_underrun ||
	    pressure > me_tx->fifo_size / 2) {
		me_tx->fifo_pos = outpos;
		me_tx->fifo_underrun = FALSE;
		pressure = 0;
	}

	vgsm_card_unlock(card);

	return pressure;
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
};

static struct kss_chan_from_ops vgsm_me_tx_node_ops =
{
	.push_raw	= vgsm_me_tx_chan_push_raw,
	.get_pressure	= vgsm_me_tx_chan_get_pressure,
};

static struct vgsm_me_tx *vgsm_me_tx_create(
	struct vgsm_me_tx *me_tx,
	struct vgsm_me *me)
{
	BUG_ON(!me_tx); /* Dyanmic allocation not implemented */
	BUG_ON(!me);

	me_tx->me = me;

	spin_lock_init(&me_tx->fifo_lock);

	init_waitqueue_head(&me_tx->wait_queue);

	me_tx->fifo_pos = 0;
	me_tx->fifo_underrun = FALSE;
	me_tx->fifo_size = me->card->writedma_size / 4;
	me_tx->codec_gain = 0xff;

	ks_chan_create(&me_tx->ks_chan,
			&vgsm_me_tx_chan_ops, "tx",
			NULL,
			&me->ks_node.kobj,
			&kss_softswitch.ks_node,
			&me->ks_node);

	me_tx->ks_chan.from_ops = &vgsm_me_tx_node_ops;

	me_tx->fifo = kfifo_alloc(
				vgsm_SERIAL_BUFF, GFP_KERNEL,
				&me_tx->fifo_lock);
	if (IS_ERR(me_tx->fifo))
		goto err_kfifo_tx;

	return me_tx;

	kfifo_free(me->tx.fifo);
err_kfifo_tx:

	return NULL;
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




static void vgsm_me_node_release(struct ks_node *node)
{
	struct vgsm_me *me =
		container_of(node, struct vgsm_me, ks_node);

	printk(KERN_DEBUG "vgsm_me_node_release()\n");

	vgsm_me_put(me);
}

static struct ks_node_ops vgsm_me_node_ops = {
	.owner			= THIS_MODULE,

	.release		= vgsm_me_node_release,
};

struct vgsm_me *vgsm_me_create(
	struct vgsm_me *me,
	struct vgsm_card *card,
	struct vgsm_micro *micro,
	int id,
	const char *name)
{
	BUG_ON(me); /* Status allocation not implemented */

	me = kmalloc(sizeof(*me), GFP_KERNEL);
	if (!me)
		goto err_kmalloc;

	memset(me, 0, sizeof(*me));

	me->micro = micro;
	me->card = card;
	me->id = id;
	me->timeslot_offset = 3 - id;

	init_completion(&me->read_status_completion);

	init_timer(&me->ack_timeout_timer);
	me->ack_timeout_timer.function = vgsm_me_ack_timeout_timer;
	me->ack_timeout_timer.data = (unsigned long)me;

	me->anal_loop = FALSE;
	me->dig_loop = FALSE;

	ks_node_create(&me->ks_node,
			&vgsm_me_node_ops, name,
			&card->pci_dev->dev.kobj);

	vgsm_me_rx_create(&me->rx, me);
	vgsm_me_tx_create(&me->tx, me);

	return me;

	kfree(me);
err_kmalloc:

	return NULL;
}

/*------------------------------------------------------------------------*/

int vgsm_me_register(struct vgsm_me *me)
{
	int err;

	tty_register_device(vgsm_tty_driver,
			me->card->id * 8 + me->id,
			&me->card->pci_dev->dev);

	/* Node is the object we are inheriting */
	err = ks_node_register(&me->ks_node);
	if (err < 0)
		goto err_node_register;

	vgsm_me_get(me);
	err = vgsm_me_rx_register(&me->rx);
	if (err < 0)
		goto err_st_me_rx_register;

	vgsm_me_get(me);
	err = vgsm_me_tx_register(&me->tx);
	if (err < 0)
		goto err_st_me_tx_register;

	return 0;

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
	vgsm_me_tx_unregister(&me->tx);
	vgsm_me_rx_unregister(&me->rx);

	ks_node_unregister(&me->ks_node);

	tty_unregister_device(vgsm_tty_driver,
			me->card->id * 8 + me->id);
}

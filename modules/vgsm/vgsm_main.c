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
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id vgsm_ids[] = {
	{ 0xe159, 0x0001, 0xa100, 0x0001, 0, 0, 0 },
	{ 0xe159, 0x0001, 0xa120, 0x0001, 0, 0, 0 },
	{ 0xe159, 0x0001, 0xa10d, 0x0001, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vgsm_ids);

struct tty_driver *vgsm_tty_driver;

struct list_head vgsm_cards_list = LIST_HEAD_INIT(vgsm_cards_list);
spinlock_t vgsm_cards_list_lock = SPIN_LOCK_UNLOCKED;

static int vgsm_tty_open(
	struct tty_struct *tty,
	struct file *file)
{
	if (!tty->driver_data) {
		// int err;
		struct vgsm_card *card;
		struct vgsm_module *module = NULL;

		spin_lock(&vgsm_cards_list_lock);
		list_for_each_entry(card, &vgsm_cards_list, cards_list_node) {
			if (card->id == tty->index / 4) {
				module = &card->modules[tty->index % 4];
				break;
			}
		}
		spin_unlock(&vgsm_cards_list_lock);

		if (!module)
			return -ENODEV;

		tty->driver_data = module;
		module->tty = tty;

		clear_bit(VGSM_MODULE_STATUS_RX_THROTTLE, &module->status);
		set_bit(VGSM_MODULE_STATUS_RUNNING, &module->status);
	} else {
		struct vgsm_module *module = tty->driver_data;

		atomic_inc(&module->tty_open_count);
	}

	return 0;
}

static void vgsm_tty_close(
	struct tty_struct *tty,
	struct file *file)
{
	struct vgsm_module *module = tty->driver_data;

	/* TTY has never ever been initialized */
	if (!module)
		return;

	if (atomic_dec_and_test(&module->tty_open_count)) {
		clear_bit(VGSM_MODULE_STATUS_RUNNING, &module->status);

		/* Flush the RX queue and ACK */
		tasklet_schedule(&module->card->rx_tasklet);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
ssize_t __kfifo_put_user(
	struct kfifo *fifo,
	const void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->size - fifo->in + fifo->out));

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, (ssize_t)(fifo->size - (fifo->in & (fifo->size - 1))));
	if (copy_from_user(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l))
		return -EFAULT;

	/* then put the rest (if any) at the beginning of the buffer */
	if (copy_from_user(fifo->buffer, buffer + l, len - l))
		return -EFAULT;

	fifo->in += len;

	return len;
}
#endif

static int vgsm_tty_write(
	struct tty_struct *tty,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
	int from_user,
#endif
	const unsigned char *buf,
	int count)
{
	struct vgsm_module *module = tty->driver_data;
	struct vgsm_card *card = module->card;
	int copied_bytes = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
	if (from_user) {
		copied_bytes = __kfifo_put_user(module->kfifo_tx,
					(unsigned char *)buf, count);
	} else {
		copied_bytes = __kfifo_put(module->kfifo_tx,
					(unsigned char *)buf, count);
	}
#else

	copied_bytes = __kfifo_put(module->kfifo_tx,
				(unsigned char *)buf, count);
#endif

	tasklet_schedule(&card->tx_tasklet);

	return copied_bytes;
}

static int vgsm_tty_write_room(struct tty_struct *tty)
{
	struct vgsm_module *module = tty->driver_data;

	return module->kfifo_tx->size - kfifo_len(module->kfifo_tx);
}

static void vgsm_tty_throttle(struct tty_struct *tty)
{
	struct vgsm_module *module = tty->driver_data;

	set_bit(VGSM_MODULE_STATUS_RX_THROTTLE, &module->status);
}

static void vgsm_tty_unthrottle(struct tty_struct *tty)
{
	struct vgsm_module *module = tty->driver_data;

	clear_bit(VGSM_MODULE_STATUS_RX_THROTTLE, &module->status);

	tasklet_schedule(&module->card->rx_tasklet);
}

static int vgsm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct vgsm_module *module = tty->driver_data;

	return kfifo_len(module->kfifo_tx);
}

static void vgsm_tty_flush_buffer(
	struct tty_struct *tty)
{
	struct vgsm_module *module = tty->driver_data;

	kfifo_reset(module->kfifo_tx);
}

static void vgsm_tty_wait_until_sent(
	struct tty_struct *tty,
	int timeout)
{
	struct vgsm_module *module = tty->driver_data;

	while(kfifo_len(module->kfifo_tx)) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&module->tx_wait_queue, &wait,
			TASK_UNINTERRUPTIBLE);

		if (kfifo_len(module->kfifo_tx))
			schedule_timeout(timeout);

		finish_wait(&module->tx_wait_queue, &wait);

		if (signal_pending(current))
			return;
	}
}

static int vgsm_tty_do_codec_set(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct vgsm_codec_ctl cctl;

	if (copy_from_user(&cctl, (void *)arg, sizeof(cctl))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	switch(cctl.parameter) {
	case VGSM_CODEC_RESET:
		vgsm_codec_reset(module->card);
	break;

	case VGSM_CODEC_RXGAIN:
		if (cctl.value < 0 || cctl.value > 0xFF) {
			err = -EINVAL;
			goto err_invalid_value;
		}

		/* codec's tx gain is analog rx gain */
		module->tx_gain = cctl.value;
	break;

	case VGSM_CODEC_TXGAIN:
		if (cctl.value < 0 || cctl.value > 0xFF) {
			err = -EINVAL;
			goto err_invalid_value;
		}

		/* codec's rx gain is analog tx gain */
		module->rx_gain = cctl.value;
	break;

	case VGSM_CODEC_DIG_LOOP:
		module->dig_loop = !!cctl.value;
	break;

	case VGSM_CODEC_ANAL_LOOP:
		module->anal_loop = !!cctl.value;
	break;
	}

	vgsm_update_codec(module);

	return 0;

err_invalid_value:
	vgsm_update_codec(module);
	vgsm_card_unlock(module->card);
err_copy_from_user:

	return err;
}

static int vgsm_tty_do_power_get(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	vgsm_card_lock(module->card);
	vgsm_module_send_power_get(module);
	vgsm_card_unlock(module->card);

	wait_for_completion_timeout(
			&module->read_status_completion, 1 * HZ);

	if (test_bit(VGSM_MODULE_STATUS_ON, &module->status))
		put_user(1, (unsigned int *)arg);
	else
		put_user(0, (unsigned int *)arg);

	return 0;
}

static int vgsm_tty_do_power_ign(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	vgsm_card_lock(module->card);
	vgsm_module_send_onoff(module, VGSM_CMD_MAINT_ONOFF_IGN);
	vgsm_card_unlock(module->card);

	msleep(150);

	vgsm_card_lock(module->card);
	vgsm_module_send_onoff(module, 0);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_tty_do_emerg_off(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	vgsm_card_lock(module->card);
	vgsm_module_send_onoff(module, VGSM_CMD_MAINT_ONOFF_EMERG_OFF);
	vgsm_card_unlock(module->card);

	msleep(3200);

	vgsm_card_lock(module->card);
	vgsm_module_send_onoff(module, 0);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_tty_do_pad_timeout(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	if (arg < 0 || arg > 0xFF)
		return -EINVAL;

	vgsm_card_lock(module->card);
	vgsm_module_send_set_padding_timeout(module, arg);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_tty_do_fw_version(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	vgsm_card_lock(module->card);
	if (module->id == 0 || module->id == 1)
		vgsm_send_get_fw_ver(&module->micro[0]);
	else
		vgsm_send_get_fw_ver(&module->micro[1]);
	vgsm_card_unlock(module->card);

	return -EOPNOTSUPP;
}

static int vgsm_tty_do_fw_upgrade(
	struct vgsm_module *module,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = module->card;
	struct vgsm_micro *micro;
	struct vgsm_fw_header fwh;
	struct vgsm_micro_message msg = { };
	int pos = 0;
	int err;
	int i;

	if (module->id == 0 || module->id == 1)
		micro = &card->micros[0];
	else
		micro = &card->micros[1];

	if (copy_from_user(&fwh, (void *)arg, sizeof(fwh))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	set_bit(VGSM_CARD_FLAGS_FW_UPGRADE, &module->card->flags);

	init_completion(&micro->fw_upgrade_ready);

	vgsm_card_lock(module->card);
	vgsm_send_fw_upgrade(micro);
	vgsm_card_unlock(module->card);

	if (!wait_for_completion_timeout(&micro->fw_upgrade_ready, 5 * HZ)) {
		err = -ETIMEDOUT;
		goto err_completion_timeout;
	}

	msg.cmd = VGSM_CMD_FW_UPGRADE;
	msg.cmd_dep = 1;
	msg.numbytes = 4;
	msg.payload[0] = fwh.size & 0xff;
	msg.payload[1] = (fwh.size & 0xff00) >> 8;
	msg.payload[2] = fwh.checksum & 0xff;
	msg.payload[3] = (fwh.checksum & 0xff00) >> 8;

	vgsm_card_lock(micro->card);
	vgsm_send_msg(micro, &msg);
	vgsm_card_unlock(micro->card);

	while(pos < fwh.size) {
		msg.cmd = VGSM_CMD_FW_UPGRADE;
		msg.cmd_dep = 2;
		msg.numbytes = min(fwh.size - pos, 7);

		if (copy_from_user(msg.payload,
				(void *)(arg + sizeof(fwh) + pos),
				sizeof(msg.payload))) {
			err = -EFAULT;
			goto err_copy_from_user_payload;
		}

		for (i=0; i<1000 && vgsm_inb(card, VGSM_PIB_E0); i++)
			msleep(1);

		vgsm_card_lock(micro->card);
		vgsm_send_msg(micro, &msg);
		vgsm_card_unlock(micro->card);

		pos += 7;
	}

	return 0; 

err_copy_from_user_payload:
err_completion_timeout:
	clear_bit(VGSM_CARD_FLAGS_FW_UPGRADE, &module->card->flags);
err_copy_from_user:

	return err;
}

static int vgsm_tty_ioctl(
	struct tty_struct *tty,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = tty->driver_data;

//	if (test_bit(VGSM_CARD_FLAGS_FW_UPGRADE, &module->card->flags))
//		return -ENOIOCTLCMD;

	switch(cmd) {
	case VGSM_IOC_GET_TX_FIFOLEN:
		return put_user(kfifo_len(module->kfifo_tx),
				(unsigned int *)arg);
	break;

	case VGSM_IOC_GET_CHANID:
		return put_user(module->visdn_chan.id, (unsigned int *)arg);
	break;

	case VGSM_IOC_CODEC_SET:
		return vgsm_tty_do_codec_set(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_GET:
		return vgsm_tty_do_power_get(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_IGN:
		return vgsm_tty_do_power_ign(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_EMERG_OFF:
		return vgsm_tty_do_emerg_off(module, cmd, arg);
	break;

	case VGSM_IOC_PAD_TIMEOUT:
		return vgsm_tty_do_pad_timeout(module, cmd, arg);
	break;

	case VGSM_IOC_FW_VERSION:
		return vgsm_tty_do_fw_version(module, cmd, arg);
	break;

	case VGSM_IOC_FW_UPGRADE:
		return vgsm_tty_do_fw_upgrade(module, cmd, arg);
	break;
	}

	return -ENOIOCTLCMD;
}

static void vgsm_tty_set_termios(
	struct tty_struct *tty,
	struct termios * old)
{
}

static struct tty_operations vgsm_tty_ops =
{
	.open			= vgsm_tty_open,
	.close			= vgsm_tty_close,
	.write			= vgsm_tty_write,
	.write_room		= vgsm_tty_write_room,
	.throttle		= vgsm_tty_throttle,
	.unthrottle		= vgsm_tty_unthrottle,
	.flush_buffer		= vgsm_tty_flush_buffer,
	.wait_until_sent	= vgsm_tty_wait_until_sent,
	.chars_in_buffer	= vgsm_tty_chars_in_buffer,
	.ioctl			= vgsm_tty_ioctl,
	.set_termios		= vgsm_tty_set_termios,
};

/* Do probing type stuff here */
static int vgsm_probe(struct pci_dev *pci_dev, 
	const struct pci_device_id *device_id_entry)
{
	
	int err;

	err = vgsm_card_probe(pci_dev, device_id_entry);
	if (err < 0)
		goto err_card_probe;

	return 0;

err_card_probe:

	return err;
}

static void vgsm_remove(struct pci_dev *pci_dev)
{
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	if (!card)
		return;

	vgsm_card_remove(card);
}

static struct pci_driver vgsm_driver =
{
	.name = 	vgsm_DRIVER_NAME,
	.id_table = 	vgsm_ids,
	.probe = 	vgsm_probe,
	.remove =	vgsm_remove,
};

#ifdef DEBUG_CODE
static ssize_t vgsm_show_debug_level(
	struct device_driver *driver,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vgsm_store_debug_level(
	struct device_driver *driver,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vgsm_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

DRIVER_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vgsm_show_debug_level,
	vgsm_store_debug_level);
#endif


static int __init vgsm_init(void)
{
	int err;

	vgsm_tty_driver = alloc_tty_driver(63);
	if (!vgsm_tty_driver) {
		err = -ENOMEM;
		goto err_alloc_tty_driver;
	}

	vgsm_tty_driver->owner = THIS_MODULE;
	vgsm_tty_driver->driver_name = vgsm_DRIVER_NAME;
	vgsm_tty_driver->name = "vgsm";
	vgsm_tty_driver->devfs_name = "vgsm/";
	vgsm_tty_driver->major = 0;
	vgsm_tty_driver->minor_start = 0;
	vgsm_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	vgsm_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	vgsm_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS ;
	vgsm_tty_driver->init_termios = tty_std_termios;
	vgsm_tty_driver->init_termios.c_cflag =
				B38400 | CS8 | CREAD | HUPCL | CLOCAL;

	tty_set_operations(vgsm_tty_driver, &vgsm_tty_ops);
	err = tty_register_driver(vgsm_tty_driver);
	if (err < 0)
		goto err_tty_register_driver;

	err = pci_register_driver(&vgsm_driver);
	if (err < 0)
		goto err_pci_register_driver;

#ifdef DEBUG_CODE
	driver_create_file(
		&vgsm_driver.driver,
		&driver_attr_debug_level);
#endif

	vgsm_msg(KERN_INFO, vgsm_DRIVER_DESCR " loaded\n");

	return 0;

#ifdef DEBUG_CODE
	driver_remove_file(
		&vgsm_driver.driver,
		&driver_attr_debug_level);
#endif

	pci_unregister_driver(&vgsm_driver);
err_pci_register_driver:
	tty_unregister_driver(vgsm_tty_driver);
err_tty_register_driver:
	put_tty_driver(vgsm_tty_driver);
err_alloc_tty_driver:

	return err;
}
module_init(vgsm_init);

static void __exit vgsm_exit(void)
{
#ifdef DEBUG_CODE
	driver_remove_file(
		&vgsm_driver.driver,
		&driver_attr_debug_level);
#endif

	pci_unregister_driver(&vgsm_driver);

	tty_unregister_driver(vgsm_tty_driver);
	put_tty_driver(vgsm_tty_driver);

	vgsm_msg(KERN_INFO, vgsm_DRIVER_DESCR " unloaded\n");
}
module_exit(vgsm_exit);

MODULE_DESCRIPTION("VoiSmart GSM Wildcard");
MODULE_AUTHOR("Daniele Orlandi <orlandi@voismart.it>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

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

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id vgsm_ids[] = {
	{ 0xe159, 0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vgsm_ids);

dev_t vgsm_first_dev;
static struct cdev vgsm_cdev;

struct list_head vgsm_cards_list = LIST_HEAD_INIT(vgsm_cards_list);
spinlock_t vgsm_cards_list_lock = SPIN_LOCK_UNLOCKED;

static int vgsm_cdev_open(
	struct inode *inode,
	struct file *file)
{
       // int err;
	struct vgsm_card *card;
	struct vgsm_module *module = NULL;

        nonseekable_open(inode, file);

	file->private_data = NULL;

	spin_lock(&vgsm_cards_list_lock);
	list_for_each_entry(card, &vgsm_cards_list, cards_list_node) {
		if (card->id == (inode->i_rdev - vgsm_first_dev) / 4) {
			module = &card->modules[(inode->i_rdev -
						vgsm_first_dev) % 4];
			break;
		}
	}
	spin_unlock(&vgsm_cards_list_lock);

	if (!module)
		return -ENODEV;

	if (test_and_set_bit(VGSM_MODULE_STATUS_OPEN, &module->status))
		return -EBUSY;

	file->private_data = module;

	set_bit(VGSM_MODULE_STATUS_RUNNING, &module->status);

	return 0;
}

static int vgsm_cdev_release(
	struct inode *inode,
	struct file *file)
{
	struct vgsm_module *module = file->private_data;

	clear_bit(VGSM_MODULE_STATUS_RUNNING, &module->status);

	while(kfifo_len(module->kfifo_tx)) {
		DEFINE_WAIT(wait);

		/*if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;*/

		prepare_to_wait(&module->tx_wait_queue, &wait,
			TASK_UNINTERRUPTIBLE);

		if (kfifo_len(module->kfifo_tx))
			schedule();

		finish_wait(&module->tx_wait_queue, &wait);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	kfifo_reset(module->kfifo_rx);
	
	clear_bit(VGSM_MODULE_STATUS_OPEN, &module->status);

	return 0;
}

ssize_t __kfifo_get_user(
	struct kfifo *fifo,
	void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->in - fifo->out));

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, (ssize_t)(fifo->size - (fifo->out & (fifo->size - 1))));
	if (copy_to_user(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)),
									l))
		return -EFAULT;

	/* then get the rest (if any) from the beginning of the buffer */
	if (copy_to_user(buffer + l, fifo->buffer, len - l))
		return -EFAULT;

	fifo->out += len;

	return len;
}

static ssize_t vgsm_cdev_read(
	struct file *file,
	char __user *user_buf,
	size_t count,
	loff_t *offp)
{
	struct vgsm_module *module = file->private_data;
	int copied_bytes;

	while(!kfifo_len(module->kfifo_rx)) {
		
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(module->rx_wait_queue,
				(kfifo_len(module->kfifo_rx))))
			return -ERESTARTSYS;
	}

	/* No locking is needed as we are the only FIFO reader */

	copied_bytes = __kfifo_get_user(module->kfifo_rx, user_buf, count);

	tasklet_schedule(&module->card->rx_tasklet);

	return copied_bytes;
}

ssize_t __kfifo_put_user(
	struct kfifo *fifo,
	const void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->size - fifo->in + fifo->out));

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, (ssize_t)(fifo->size - (fifo->in & (fifo->size - 1))));
	if (copy_from_user(fifo->buffer + (fifo->in & (fifo->size - 1)),
								buffer, l))
		return -EFAULT;

	/* then put the rest (if any) at the beginning of the buffer */
	if (copy_from_user(fifo->buffer, buffer + l, len - l))
		return -EFAULT;

	fifo->in += len;

	return len;
}

static ssize_t vgsm_cdev_write(
	struct file *file,
	const char __user *user_buf,
	size_t count,
	loff_t *offp)
{
	struct vgsm_module *module = file->private_data;
	struct vgsm_card *card = module->card;
	int copied_bytes = 0;

	if (count >= module->kfifo_tx->size)
		return -E2BIG;

	while(module->kfifo_tx->size - kfifo_len(module->kfifo_tx) < count) {
		DEFINE_WAIT(wait);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		prepare_to_wait(&module->tx_wait_queue, &wait,
			TASK_UNINTERRUPTIBLE);

		if (module->kfifo_tx->size - kfifo_len(module->kfifo_tx) <
									count)
			schedule();

		finish_wait(&module->tx_wait_queue, &wait);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	copied_bytes = __kfifo_put_user(module->kfifo_tx, user_buf, count);

	tasklet_schedule(&card->tx_tasklet);

	return copied_bytes;
}

static unsigned int vgsm_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	struct vgsm_module *module = file->private_data;
	unsigned int res = 0;

	poll_wait(file, &module->rx_wait_queue, wait);
	poll_wait(file, &module->tx_wait_queue, wait);

	if (kfifo_len(module->kfifo_rx))
		res |= POLLIN | POLLRDNORM;

	if (module->kfifo_tx->size - kfifo_len(module->kfifo_tx) > 0)
		res |= POLLOUT;

	return res;
}

static int vgsm_cdev_do_codec_set(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct vgsm_codec_ctl cctl;
	struct vgsm_module *module = file->private_data;

	if (copy_from_user(&cctl, (void *)arg, sizeof(cctl))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	vgsm_card_lock(module->card);

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

	vgsm_card_unlock(module->card);

	return 0;

err_invalid_value:
	vgsm_update_codec(module);
	vgsm_card_unlock(module->card);
err_copy_from_user:

	return err;
}

static int vgsm_cdev_do_power(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = file->private_data;

	if (arg == 0) {
		vgsm_card_lock(module->card);
		vgsm_module_send_onoff(module, VGSM_CMD_MAINT_ONOFF_ON);
		vgsm_card_unlock(module->card);

		msleep(1500);
	} else if (arg == 1) {
		vgsm_card_lock(module->card);
		vgsm_module_send_onoff(module, VGSM_CMD_MAINT_ONOFF_UNCOND_OFF);
		vgsm_card_unlock(module->card);

		msleep(200);

	}

	vgsm_card_lock(module->card);
	vgsm_module_send_onoff(module, 0);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_cdev_do_pad_timeout(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = file->private_data;

	if (arg < 0 || arg > 0xFF)
		return -EINVAL;

	vgsm_card_lock(module->card);
	vgsm_module_send_set_padding_timeout(module, arg);
	vgsm_card_unlock(module->card);

	return 0;
}

static int vgsm_cdev_do_fw_version(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = file->private_data;

	vgsm_card_lock(module->card);
	if (module->id == 0 || module->id == 1)
		vgsm_send_get_fw_ver(module->card, 0);
	else
		vgsm_send_get_fw_ver(module->card, 1);
	vgsm_card_unlock(module->card);

	return -EOPNOTSUPP;
}

static int vgsm_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_module *module = file->private_data;

	switch(cmd) {
	case VGSM_IOC_GET_CHANID:
		return put_user(module->visdn_chan.id, (unsigned int *)arg);
	break;

	case VGSM_IOC_CODEC_SET:
		return vgsm_cdev_do_codec_set(inode, file, cmd, arg);
	break;

	case VGSM_IOC_POWER:
		return vgsm_cdev_do_power(inode, file, cmd, arg);
	break;

	case VGSM_IOC_PAD_TIMEOUT:
		return vgsm_cdev_do_pad_timeout(inode, file, cmd, arg);
	break;

	case VGSM_IOC_FW_VERSION:
		return vgsm_cdev_do_fw_version(inode, file, cmd, arg);
	break;
	}

	return -EOPNOTSUPP;
}

static struct file_operations vgsm_fops =
{
	.owner		= THIS_MODULE,
	.open		= vgsm_cdev_open,
	.release	= vgsm_cdev_release,
	.read		= vgsm_cdev_read,
	.write		= vgsm_cdev_write,
	.poll		= vgsm_cdev_poll,
	.ioctl		= vgsm_cdev_ioctl,
	.llseek		= no_llseek,
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

static void vgsm_class_release(struct class_device *device)
{
	printk(KERN_INFO "vgsm_class_release()\n");
}

struct class vgsm_class =
{
	.name = "vgsm-serial",
	.release = vgsm_class_release,
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

	/* Allocate and register a cdev */
	err = alloc_chrdev_region(&vgsm_first_dev, 0, 256, vgsm_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vgsm_cdev, &vgsm_fops);
	vgsm_cdev.owner = THIS_MODULE;

	err = cdev_add(&vgsm_cdev, vgsm_first_dev, 256);
	if (err < 0)
		goto err_cdev_add;

	err = class_register(&vgsm_class);
	if (err < 0)
		goto err_class_register;

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
	class_unregister(&vgsm_class);
err_class_register:
	cdev_del(&vgsm_cdev);
err_cdev_add:
	unregister_chrdev_region(vgsm_first_dev, 256);
err_register_chrdev:

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

	class_unregister(&vgsm_class);

	cdev_del(&vgsm_cdev);

	unregister_chrdev_region(vgsm_first_dev, 256);

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

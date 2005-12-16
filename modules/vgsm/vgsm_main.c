/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi, Massimo Mazzeo
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

	file->private_data = module;

	return 0;
}

static int vgsm_cdev_release(
	struct inode *inode,
	struct file *file)
{
	return 0;
}

static ssize_t vgsm_cdev_read(
	struct file *file,
	char __user *user_buf,
	size_t count,
	loff_t *offp)
{
	struct vgsm_module *module = file->private_data;
	int copied_bytes = 0;

	DEFINE_WAIT(wait);

	u8 tmpbuf[256];

printk(KERN_CRIT "a\n");
	while(!kfifo_len(module->kfifo_rx)) {
		
printk(KERN_CRIT "b\n");
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

printk(KERN_CRIT "c\n");
		if (wait_event_interruptible(module->rx_wait_queue,
				(kfifo_len(module->kfifo_rx))))
			return -ERESTARTSYS;
	}
printk(KERN_CRIT "d\n");

//out:

	copied_bytes = kfifo_get(module->kfifo_rx, tmpbuf, count);

	if (copy_to_user(user_buf, tmpbuf, copied_bytes))
		return -EFAULT;

	return copied_bytes;
}

static ssize_t vgsm_cdev_write(
	struct file *file,
	const char __user *user_buf,
	size_t count,
	loff_t *offp)
{
	struct vgsm_module *module = file->private_data;
	struct vgsm_card *card = module->card;
	u8 buf[7];
	int copied_bytes = 0;
	int bytes_to_send;
	int err;

	DEFINE_WAIT(wait);

	while(copied_bytes < count) {
		int timeout;

		/* Acquire buffer */
		for (;;) {
			vgsm_card_lock(card);
			if (vgsm_inb(card, VGSM_PIB_E0) == 0)
				break;

			vgsm_card_unlock(card);
			schedule();
			/* TODO: timeout after being unable to acquire
			   buffer for too much time */
		}

		bytes_to_send = min(sizeof(buf), count - copied_bytes);
		if (copy_from_user(buf, user_buf, bytes_to_send)) {
			vgsm_card_unlock(card);
			return -EFAULT;
		}

		vgsm_module_send_string(module, buf, bytes_to_send);

		vgsm_card_unlock(card);

		prepare_to_wait(&module->tx_wait_queue, &wait,
			TASK_UNINTERRUPTIBLE);

		timeout = schedule_timeout(2 * HZ);
		if (!timeout) {
			// TIMEOUT!!! FIXME XXX
			vgsm_module_send_ack(module);
			err = -ETIMEDOUT;
			goto err_timeout;
		}

		copied_bytes += bytes_to_send;
	}

	finish_wait(&module->tx_wait_queue, &wait);

	return copied_bytes;

err_timeout:

	finish_wait(&module->tx_wait_queue, &wait);

	return err;
}

static unsigned int vgsm_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	struct vgsm_module *module = file->private_data;

	poll_wait(file, &module->rx_wait_queue, wait);
	poll_wait(file, &module->tx_wait_queue, wait);

	if (kfifo_len(module->kfifo_rx))
		return POLLIN | POLLRDNORM | POLLOUT;
	else
		return POLLOUT;
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
	case VGSM_CODEC_RXGAIN:
		if (cctl.value < 0 || cctl.value > 0xFF) {
			err = -EINVAL;
			goto err_invalid_value;
		}
	break;

	case VGSM_CODEC_TXGAIN:
		if (cctl.value < 0 || cctl.value > 0xFF) {
			err = -EINVAL;
			goto err_invalid_value;
		}
	break;

	case VGSM_CODEC_RXPRE:
		module->rx_pre = !!cctl.value;
	break;

	case VGSM_CODEC_TXPRE:
		module->tx_pre = !!cctl.value;
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

static int vgsm_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VGSM_IOC_CODEC_SET:
		return vgsm_cdev_do_codec_set(inode, file, cmd, arg);
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

static int vgsm_class_hotplug(struct class_device *device, 
	char **envp,
	int num_envp, 
	char *buf, 
	int size)
{
	printk(KERN_INFO "vgsm_class_hotplug()\n");

	envp[0] = NULL;

	return 0;
}

struct class vgsm_class =
{
	.name = "vgsm-serial",
	.release = vgsm_class_release,
	.hotplug = vgsm_class_hotplug,
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

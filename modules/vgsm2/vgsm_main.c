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
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vgsm_ids);

struct tty_driver *vgsm_tty_driver;

struct list_head vgsm_cards_list = LIST_HEAD_INIT(vgsm_cards_list);
spinlock_t vgsm_cards_list_lock = SPIN_LOCK_UNLOCKED;

#if 0
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

	case VGSM_IOC_POWER_GET:
		return vgsm_tty_do_power_get(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_IGN:
		return vgsm_tty_do_power_ign(module, cmd, arg);
	break;

	case VGSM_IOC_POWER_EMERG_OFF:
		return vgsm_tty_do_emerg_off(module, cmd, arg);
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
#endif

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
	.name		= vgsm_DRIVER_NAME,
	.id_table	= vgsm_ids,
	.probe		= vgsm_probe,
	.remove		= vgsm_remove,
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

	vgsm_msg(KERN_INFO, vgsm_DRIVER_DESCR " unloaded\n");
}
module_exit(vgsm_exit);

MODULE_DESCRIPTION("VoiSmart vGSM-II");
MODULE_AUTHOR("Daniele Orlandi <orlandi@voismart.it>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

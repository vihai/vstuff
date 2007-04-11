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
#include <linux/termios.h>
#include <linux/serial_core.h>

#include <linux/kstreamer/dynattr.h>

#include "vgsm2.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"
#include "sim.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id vgsm_ids[] = {
	{ 0xf16a, 0x0004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vgsm_ids);

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

static int vgsm_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry)
{
	int err;
	static int numcards;

	struct vgsm_card *card;

	card = vgsm_card_create(NULL,
		pci_dev,
		numcards++);
	if (!card) {
		err = -ENOMEM;
		goto err_card_create;
	}

	err = vgsm_card_probe(card);
	if (err < 0)
		goto err_card_probe;

	pci_set_drvdata(pci_dev, card);

	err = vgsm_card_register(card);
	if (err < 0)
		goto err_card_register;

	return 0;

	vgsm_card_unregister(card);
err_card_register:
	vgsm_card_remove(card);
err_card_probe:
	vgsm_card_put(card);
err_card_create:

	return err;
}

static void vgsm_remove(struct pci_dev *pci_dev)
{
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	if (!card)
		return;

	vgsm_card_unregister(card);
	vgsm_card_remove(card);
	vgsm_card_destroy(card);
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

struct ks_dynattr *vgsm_amu_compander_class;
struct ks_dynattr *vgsm_amu_decompander_class;

static int __init vgsm_init(void)
{
	int err;

	vgsm_amu_compander_class = ks_dynattr_register("amu_compander");
	if (!vgsm_amu_compander_class) {
		err = -ENOMEM;
		goto err_register_amu_compander;
	}

	vgsm_amu_decompander_class = ks_dynattr_register("amu_decompander");
	if (!vgsm_amu_decompander_class) {
		err = -ENOMEM;
		goto err_register_amu_decompander;
	}

	err = vgsm_card_modinit();
	if (err < 0)
		goto err_card_modinit;

	err = vgsm_module_modinit();
	if (err < 0)
		goto err_module_modinit;

	err = vgsm_sim_modinit();
	if (err < 0)
		goto err_sim_modinit;

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
	vgsm_sim_modexit();
err_sim_modinit:
	vgsm_module_modexit();
err_module_modinit:
	vgsm_card_modexit();
err_card_modinit:
	ks_dynattr_unregister(vgsm_amu_decompander_class);
err_register_amu_decompander:
	ks_dynattr_unregister(vgsm_amu_compander_class);
err_register_amu_compander:

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

	vgsm_sim_modexit();
	vgsm_module_modexit();
	vgsm_card_modexit();

	ks_dynattr_unregister(vgsm_amu_decompander_class);
	ks_dynattr_unregister(vgsm_amu_compander_class);

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

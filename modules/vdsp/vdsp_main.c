/*
 * VoiSmart vDSP board driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

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

#include "vdsp.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id vdsp_ids[] = {
	{ 0x104c, 0x9065, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vdsp_ids);

static int vdsp_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry)
{
	int err;

	err = vdsp_card_probe(pci_dev, device_id_entry);
	if (err < 0)
		goto err_card_probe;

	return 0;

err_card_probe:

	return err;
}

static void vdsp_remove(struct pci_dev *pci_dev)
{
	struct vdsp_card *card = pci_get_drvdata(pci_dev);

	if (!card)
		return;

	vdsp_card_remove(card);
}

static struct pci_driver vdsp_driver =
{
	.name		= vdsp_DRIVER_NAME,
	.id_table	= vdsp_ids,
	.probe		= vdsp_probe,
	.remove		= vdsp_remove,
};

#ifdef DEBUG_CODE
static ssize_t vdsp_show_debug_level(
	struct device_driver *driver,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vdsp_store_debug_level(
	struct device_driver *driver,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vdsp_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

DRIVER_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vdsp_show_debug_level,
	vdsp_store_debug_level);
#endif


static int __init vdsp_init(void)
{
	int err;


	err = pci_register_driver(&vdsp_driver);
	if (err < 0)
		goto err_pci_register_driver;

#ifdef DEBUG_CODE
	err = driver_create_file(
		&vdsp_driver.driver,
		&driver_attr_debug_level);
	if (err < 0)
		goto err_create_file_debug_level;
#endif

	vdsp_msg(KERN_INFO, vdsp_DRIVER_DESCR " loaded\n");

	return 0;

#ifdef DEBUG_CODE
	driver_remove_file(
		&vdsp_driver.driver,
		&driver_attr_debug_level);
#endif

#ifdef DEBUG_CODE
	driver_remove_file(
		&vdsp_driver.driver,
		&driver_attr_debug_level);
err_create_file_debug_level:
#endif
	pci_unregister_driver(&vdsp_driver);
err_pci_register_driver:

	return err;
}
module_init(vdsp_init);

static void __exit vdsp_exit(void)
{
#ifdef DEBUG_CODE
	driver_remove_file(
		&vdsp_driver.driver,
		&driver_attr_debug_level);
#endif

	pci_unregister_driver(&vdsp_driver);

	vdsp_msg(KERN_INFO, vdsp_DRIVER_DESCR " unloaded\n");
}
module_exit(vdsp_exit);

MODULE_DESCRIPTION("VoiSmart vDSP");
MODULE_AUTHOR("Daniele Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <linux/visdn/core.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "pcm_port.h"
#include "pcm_port_inline.h"
#include "st_port.h"
#include "st_chan.h"
#include "sys_port.h"
#include "sys_chan.h"
#include "fifo.h"
#include "fifo_inline.h"
#include "card.h"
#include "card_inline.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

static struct pci_device_id hfc_pci_ids[] = {
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_ANY_ID, 0xb520, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32,
			 }},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 0,
			.ram_size = 32,
			 }},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_8S,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 0,
			.ram_size = 32,
			 }},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hfc_pci_ids);

static int __devinit hfc_probe(struct pci_dev *dev
			, const struct pci_device_id *ent);
static void __devexit hfc_remove(struct pci_dev *dev);

static struct pci_driver hfc_driver = {
	.name     = hfc_DRIVER_NAME,
	.id_table = hfc_pci_ids,
	.probe    = hfc_probe,
	.remove   = hfc_remove,
};

static int __devinit hfc_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry)
{
	int err;

	err = hfc_card_probe(pci_dev, device_id_entry);
	if (err < 0)
		goto err_card_probe;

	return 0;

err_card_probe:

	return err;
}

static void __devexit hfc_remove(struct pci_dev *pci_dev)
{
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	if (!card)
		return;

	hfc_card_remove(card);
}

#ifdef DEBUG_CODE
static ssize_t hfc_show_debug_level(
	struct device_driver *driver,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t hfc_store_debug_level(
	struct device_driver *driver,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	hfc_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

DRIVER_ATTR(debug_level, S_IRUGO | S_IWUSR,
	hfc_show_debug_level,
	hfc_store_debug_level);
#endif

/******************************************
 * Module stuff
 ******************************************/

static int __init hfc_init_module(void)
{
	int err;

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " loading\n");

	err = pci_register_driver(&hfc_driver);
	if (err < 0)
		goto err_pci_register_driver;

#ifdef DEBUG_CODE
	err = driver_create_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif

	return 0;

#ifdef DEBUG_CODE
	driver_remove_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif
err_pci_register_driver:

	return err;
}

module_init(hfc_init_module);

static void __exit hfc_module_exit(void)
{
#ifdef DEBUG_CODE
	driver_remove_file(
		&hfc_driver.driver,
		&driver_attr_debug_level);
#endif

	pci_unregister_driver(&hfc_driver);

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " unloaded\n");
}

module_exit(hfc_module_exit);

MODULE_DESCRIPTION(hfc_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif

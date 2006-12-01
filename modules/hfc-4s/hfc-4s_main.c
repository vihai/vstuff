/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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

#include <linux/kstreamer/dynattr.h>

#include "card.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 4;
#else
int debug_level = 0;
#endif
#endif

#ifndef PCI_DEVICE_ID_CCD_HFC_4S
#define PCI_DEVICE_ID_CCD_HFC_4S	0x08b4
#endif

#ifndef PCI_SUBDEVICE_ID_CCD_HFC_4S
#define PCI_SUBDEVICE_ID_CCD_HFC_4S	0x08b4
#endif

#ifndef PCI_SUBDEVICE_ID_CCD_BERONET
#define PCI_SUBDEVICE_ID_CCD_BERONET	0xb520
#endif

#ifndef PCI_SUBDEVICE_ID_CCD_JUNGHANNS
#define PCI_SUBDEVICE_ID_CCD_JUNGHANNS	0xb550
#endif

#ifndef PCI_SUBDEVICE_ID_CCD_JUNGHANNS2
#define PCI_SUBDEVICE_ID_CCD_JUNGHANNS2	0xb560
#endif

#ifndef PCI_SUBDEVICE_ID_CCD_JUNGHANNS3
#define PCI_SUBDEVICE_ID_CCD_JUNGHANNS3	0xb552
#endif

#ifndef PCI_DEVICE_ID_CCD_HFC_8S
#define PCI_DEVICE_ID_CCD_HFC_8S	0x16B8
#endif

#ifndef PCI_VENDOR_ID_HST
#define PCI_VENDOR_ID_HST		0x136a
#endif

#ifndef PCI_VENDOR_ID_HST_HFC_4S
#define PCI_VENDOR_ID_HST_HFC_4S	0x0007
#endif

static struct pci_device_id hfc_pci_ids[] = {
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_VENDOR_ID_CCD, PCI_SUBDEVICE_ID_CCD_HFC_4S, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 0,
			.ram_size = 32,
			.pwm0 = 0x1e,
			.pwm1 = 0x1e,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xe,
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_VENDOR_ID_CCD, PCI_SUBDEVICE_ID_CCD_BERONET, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32,
			.pwm0 = 0x1e,
			.pwm1 = 0x1e,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xe,
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_VENDOR_ID_CCD, PCI_SUBDEVICE_ID_CCD_JUNGHANNS, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32,
			.pwm0 = 0x1e,
			.pwm1 = 0x1e,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xe,
			// Implement LED scheme
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_VENDOR_ID_CCD, PCI_SUBDEVICE_ID_CCD_JUNGHANNS2, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32,
			.pwm0 = 0x1e,
			.pwm1 = 0x1e,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xf,
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_4S,
		PCI_VENDOR_ID_CCD, PCI_SUBDEVICE_ID_CCD_JUNGHANNS3, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32, // Probably more, FIXME
			.pwm0 = 0x19,
			.pwm1 = 0x19,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xf,
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{
		PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_HFC_8S,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		(unsigned long)&(struct hfc_card_config) {
			.double_clock = 0,
			.quartz_49 = 1,
			.ram_size = 32,
			.pwm0 = 0x1e,
			.pwm1 = 0x1e,
			.clk_dly_nt = 0xc,
			.clk_dly_te = 0xe,
			.sampl_comp_nt = 0x6,
			.sampl_comp_te = 0x6,
			 }
	},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hfc_pci_ids);

static int __devinit hfc_probe(
	struct pci_dev *pci_dev,
	const struct pci_device_id *device_id_entry)
{
	int err;

	struct hfc_card *card;
	card = hfc_card_alloc();
	if (!card) {
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	hfc_card_init(card,
		pci_dev,
		(struct hfc_card_config *)device_id_entry->driver_data);

	err = hfc_card_probe(card);
	if (err < 0)
		goto err_card_probe;

	pci_set_drvdata(pci_dev, card);

	/* Ok, the hardware is ready and the data structures are initialized,
	 * we can now register to the system.
	 */

	err = hfc_card_register(card);
	if (err < 0)
		goto err_card_register;

	return 0;

	hfc_card_unregister(card);
err_card_register:
	hfc_card_remove(card);
err_card_probe:
	hfc_card_put(card);
err_alloc_hfccard:

	return err;
}

static void __devexit hfc_remove(struct pci_dev *pci_dev)
{
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	if (!card)
		return;

	hfc_card_unregister(card);
	hfc_card_remove(card);
}

static struct pci_driver hfc_driver = {
	.name     = hfc_DRIVER_NAME,
	.id_table = hfc_pci_ids,
	.probe    = hfc_probe,
	.remove   = hfc_remove,
};


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

struct ks_dynattr *hfc_hdlc_framer_class;
struct ks_dynattr *hfc_hdlc_deframer_class;
struct ks_dynattr *hfc_octet_reverser_class;

static int __init hfc_init_module(void)
{
	int err;

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " loading\n");

	hfc_hdlc_framer_class = ks_dynattr_register("hdlc_framer");
	if (!hfc_hdlc_framer_class) {
		err = -ENOMEM;
		goto err_register_hdlc_framer;
	}

	hfc_hdlc_deframer_class = ks_dynattr_register("hdlc_deframer");
	if (!hfc_hdlc_deframer_class) {
		err = -ENOMEM;
		goto err_register_hdlc_deframer;
	}

	hfc_octet_reverser_class = ks_dynattr_register("octet_reverser");
	if (!hfc_octet_reverser_class) {
		err = -ENOMEM;
		goto err_register_octet_reverser;
	}

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
	ks_dynattr_unregister(hfc_octet_reverser_class);
err_register_octet_reverser:
	ks_dynattr_unregister(hfc_hdlc_deframer_class);
err_register_hdlc_deframer:
	ks_dynattr_unregister(hfc_hdlc_framer_class);
err_register_hdlc_framer:

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

	ks_dynattr_unregister(hfc_octet_reverser_class);
	ks_dynattr_unregister(hfc_hdlc_deframer_class);
	ks_dynattr_unregister(hfc_hdlc_framer_class);

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

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

#include <linux/kstreamer/feature.h>

#include "vgsm2.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"
#include "sim.h"

#ifdef DEBUG_CODE
int debug_level = 0;
#endif

#ifndef PCI_VENDOR_ID_ESPIA
#define PCI_VENDOR_ID_ESPIA	0x1ab9
#endif

#ifndef PCI_DEVICE_ID_ESPIA_VGSM2
#define PCI_DEVICE_ID_ESPIA_VGSM2	0x0001
#endif

unsigned long vgsm_status = 0;

static struct pci_device_id vgsm_ids[] = {
	{ PCI_VENDOR_ID_ESPIA, PCI_DEVICE_ID_ESPIA_VGSM2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, vgsm_ids);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
struct work_struct vgsm_led_update_work;
#else
struct delayed_work vgsm_led_update_work;
#endif

static BOOL vgsm_card_led_update(struct vgsm_card *card)
{
	BOOL reschedule = FALSE;
	u32 led_src = 0;
	u32 led_user = 0;
	int i;

	u32 color;
	switch((jiffies / (HZ / 5)) & 0x3) {
	default:
	case 0: color = 0xffffffff; break;
	case 1: color = 0x55555555; break;
	case 2: color = 0xaaaaaaaa; break;
	case 3: color = 0x00000000; break;
	}

	if (test_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags)) {
		led_src = 0xffffff;
		led_user = color & 0xffffff;
		reschedule = TRUE;
	} else {
		for(i=0; i<card->mes_number; i++) {
			if (card->modules[i] &&
			     test_bit(VGSM_MODULE_STATUS_IDENTIFY,
						&card->modules[i]->status)) {

				led_src |= 0xf << (i * 4);
				led_user |= color << (i * 4);

				if (card->modules[i]->route_to_sim >= 0) {
					int sim = card->modules[i]->
								route_to_sim;

					led_src |= 0x3 << ((sim * 2) + 16);
					led_user |= (color & 0x3) <<
							((sim * 2) + 16);
				}

				reschedule = TRUE;
			}
		}

		for(i=0; i<card->sims_number; i++) {
			if (test_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags) ||
			    test_bit(VGSM_MODULE_STATUS_IDENTIFY,
						&card->sims[i].status)) {

				led_src |= 0x3 << ((i * 2) + 16);
				led_user |= (color & 0x3) << ((i * 2) + 16);
				reschedule = TRUE;
			}
		}
	}

	if (test_bit(VGSM_CARD_FLAGS_RECONFIG_PENDING, &card->flags)) {
		if ((jiffies / (HZ / 5)) & 0x1) {
			led_src |= VGSM_R_LED_SRC_V_STATUS_R;
			led_user |= VGSM_R_LED_USER_V_STATUS_R;
		}

		reschedule = TRUE;
	}

	vgsm_outl(card, VGSM_R_LED_SRC, led_src);
	vgsm_outl(card, VGSM_R_LED_USER, led_user);

	return reschedule;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vgsm_led_update_work_func(void *data)
#else
static void vgsm_led_update_work_func(struct work_struct *work)
#endif
{
	struct vgsm_card *card;
	BOOL reschedule = FALSE;

	spin_lock(&vgsm_cards_list_lock);
	list_for_each_entry(card, &vgsm_cards_list, cards_list_node) {

		if (test_bit(VGSM_CARD_FLAGS_READY, &card->flags)) 
			reschedule = reschedule ||
					vgsm_card_led_update(card);
	}
	spin_unlock(&vgsm_cards_list_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	if (reschedule &&
	    !test_big(VGSM_STATUS_FLAG_SHUTTING_DOWN, &vgsm_status))
		schedule_delayed_work(&vgsm_led_update_work, HZ / 5);
#else
	if (reschedule)
		schedule_delayed_work(&vgsm_led_update_work, HZ / 5);
#endif
}

void vgsm_led_update(void)
{
	schedule_delayed_work(&vgsm_led_update_work, HZ / 5);
}

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

struct ks_feature *vgsm_amu_compander_class;
struct ks_feature *vgsm_amu_decompander_class;

static int __init vgsm_init(void)
{
	int err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&vgsm_led_update_work,
		vgsm_led_update_work_func,
		NULL);
#else
	INIT_DELAYED_WORK(&vgsm_led_update_work,
		vgsm_led_update_work_func);
#endif

	vgsm_amu_compander_class = ks_feature_register("amu_compander");
	if (!vgsm_amu_compander_class) {
		err = -ENOMEM;
		goto err_register_amu_compander;
	}

	vgsm_amu_decompander_class = ks_feature_register("amu_decompander");
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
	err = driver_create_file(
		&vgsm_driver.driver,
		&driver_attr_debug_level);
	if (err < 0)
		goto err_create_file_debug_level;
#endif

	vgsm_msg(KERN_INFO, vgsm_DRIVER_DESCR " loaded\n");

	return 0;

#ifdef DEBUG_CODE
	driver_remove_file(
		&vgsm_driver.driver,
		&driver_attr_debug_level);
err_create_file_debug_level:
#endif

	pci_unregister_driver(&vgsm_driver);
err_pci_register_driver:
	vgsm_sim_modexit();
err_sim_modinit:
	vgsm_module_modexit();
err_module_modinit:
	vgsm_card_modexit();
err_card_modinit:
	ks_feature_unregister(vgsm_amu_decompander_class);
err_register_amu_decompander:
	ks_feature_unregister(vgsm_amu_compander_class);
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	set_bit(VGSM_STATUS_FLAG_SHUTTING_DOWN, &vgsm_status);
	cancel_delayed_work(&vgsm_led_update_work);
	flush_scheduled_work();
#else
	cancel_rearming_delayed_work(&vgsm_led_update_work);
#endif

	pci_unregister_driver(&vgsm_driver);

	vgsm_sim_modexit();
	vgsm_module_modexit();
	vgsm_card_modexit();

	ks_feature_unregister(vgsm_amu_decompander_class);
	ks_feature_unregister(vgsm_amu_compander_class);

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

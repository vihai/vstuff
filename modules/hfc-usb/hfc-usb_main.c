/*
 * Cologne Chip's HFC-USB vISDN driver
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/delay.h>

#include "hfc-usb.h"
#include "card.h"
#include "card_inline.h"
#include "fifo.h"
#include "fifo_inline.h"

#define D 0
#define B1 1
#define B2 2
#define E 3

#define D_FIFO_OFF 2
#define B1_FIFO_OFF 0
#define B2_FIFO_OFF 1
#define E_FIFO_OFF 3

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 4;
#else
int debug_level = 0;
#endif
#endif

#define HFC_VENDOR_ID_COLOGNE	0x0959
#define HFC_VENDOR_ID_TRUST	0x07b0

static struct usb_device_id hfc_table [] = {
	{ USB_DEVICE(HFC_VENDOR_ID_COLOGNE, 0x2bd0) },
	{ USB_DEVICE(HFC_VENDOR_ID_TRUST, 0x0006) },
	{ USB_DEVICE(0x0675, 0x1688) },	/* DrayTek miniVigor 128 USB ISDN TA */
	{ USB_DEVICE(0x07b0, 0x0007) },	/* Billion USB TA 2 */
	{ USB_DEVICE(0x0742, 0x2008) },	/* Stollmann USB TA */
	{ USB_DEVICE(0x0742, 0x2009) },	/* Aceex USB ISDN TA */
	{ USB_DEVICE(0x0742, 0x200A) },	/* OEM USB ISDN TA */
	{ USB_DEVICE(0x08e3, 0x0301) },	/* OliTec ISDN USB */
	{ USB_DEVICE(0x07fa, 0x0846) },	/* Bewan ISDN USB TA */
	{ USB_DEVICE(0x07fa, 0x0847) },	/* Djinn Numeris USB */
	{ USB_DEVICE(0x07b0, 0x0006) },	/* Twister ISDN USB TA */
	{ }
};
MODULE_DEVICE_TABLE (usb, hfc_table);

static void hfc_card_reset(struct hfc_card *card)
{
	hfc_write(card, HFC_REG_CIRM,
		HFC_REG_CIRM_AUX_FIXED | HFC_REG_CIRM_RESET);
	hfc_write(card, HFC_REG_CIRM,
		HFC_REG_CIRM_AUX_FIXED);
}

static void hfc_card_initialize(struct hfc_card *card)
{
	hfc_write(card, HFC_REG_USB_SIZE,
		HFC_REG_USB_SIZE_OUT(
			card->fifos[FIFO_D][TX].
			int_endpoint->desc.wMaxPacketSize) |
		HFC_REG_USB_SIZE_IN(
			card->fifos[FIFO_D][RX].
			int_endpoint->desc.wMaxPacketSize));

	hfc_write(card, HFC_REG_USB_SIZE_I, 0x10);

//	hfc_write(card, HFC_REG_USB_SIZE_I,
//		HFC_REG_USB_SIZE_I_VAL(card->chans[D].tx.iso_endpoint->desc.wMaxPacketSize));

	hfc_write(card, HFC_REG_F_THRESH,
		HFC_REG_F_THRESH_TX(1) |
		HFC_REG_F_THRESH_RX(1));

	hfc_write(card, HFC_REG_MST_MODE0,
		HFC_REG_MST_MODE0_MASTER);

	hfc_write(card, HFC_REG_MST_MODE1,
		HFC_REG_MST_MODE1_DPLL_4 |
		HFC_REG_MST_MODE1_PCM30);

	hfc_write(card, HFC_REG_MST_MODE2,
		HFC_REG_MST_MODE2_PCM_31_0);

	hfc_write(card, HFC_REG_SCTRL_E, 0);

	hfc_st_port_update_sctrl(&card->st_port);
	hfc_st_port_update_sctrl_r(&card->st_port);
	hfc_st_port_update_st_clk_dly(&card->st_port);
}

static int hfc_probe(struct usb_interface *usb_intf,
	const struct usb_device_id *id)
{
	struct hfc_card *card;
	int err = 0;
	int i;

	struct usb_host_interface *usb_host_intf;
	int alt_idx = 0;
	for (alt_idx = 0; alt_idx < usb_intf->num_altsetting; alt_idx++) {
		usb_host_intf = &usb_intf->altsetting[alt_idx];

		if (usb_host_intf->desc.bInterfaceClass == 0x0a &&
		    usb_host_intf->desc.bNumEndpoints == 8 &&
		    usb_intf->altsetting[alt_idx].
				endpoint[0].desc.bEndpointAddress == 0x01)
			goto found;
	}

	return -ENODEV;

found:

	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto err_kmalloc;
	}

	memset(card, 0, sizeof(*card));

	card->usb_dev = usb_get_dev(interface_to_usbdev(usb_intf));
	card->usb_interface = usb_intf;

	usb_set_interface(card->usb_dev,
		usb_host_intf->desc.bInterfaceNumber, alt_idx);

	usb_set_intfdata(usb_intf, card);

	init_MUTEX(&card->sem);

	hfc_fifo_init(&card->fifos[FIFO_B1][TX], card, 0, TX, "B1");
	hfc_fifo_init(&card->fifos[FIFO_B1][RX], card, 1, RX, "B1");
	hfc_fifo_init(&card->fifos[FIFO_B2][TX], card, 2, TX, "B2");
	hfc_fifo_init(&card->fifos[FIFO_B2][RX], card, 3, RX, "B2");
	hfc_fifo_init(&card->fifos[FIFO_D][TX], card, 4, TX, "D");
	hfc_fifo_init(&card->fifos[FIFO_D][RX], card, 5, RX, "D");
	hfc_fifo_init(&card->fifos[FIFO_PCME][TX], card, 6, TX, "PCM/E");
	hfc_fifo_init(&card->fifos[FIFO_PCME][RX], card, 7, RX, "PCM/E");

	for(i = 0; i < card->usb_interface->cur_altsetting->desc.bNumEndpoints;
	    i++) {

		struct usb_host_endpoint *ep;
		ep = &card->usb_interface->cur_altsetting->endpoint[i];

		switch(ep->desc.bEndpointAddress) {
		case 0x01: card->fifos[FIFO_B1][TX].int_endpoint = ep; break;
		case 0x81: card->fifos[FIFO_B1][RX].int_endpoint = ep; break;
		case 0x02: card->fifos[FIFO_B2][TX].int_endpoint = ep; break;
		case 0x82: card->fifos[FIFO_B2][RX].int_endpoint = ep; break;
		case 0x03: card->fifos[FIFO_D][TX].int_endpoint = ep; break;
		case 0x83: card->fifos[FIFO_D][RX].int_endpoint = ep; break;
		case 0x04: card->fifos[FIFO_PCME][TX].int_endpoint = ep; break;
		case 0x84: card->fifos[FIFO_PCME][RX].int_endpoint = ep; break;

		case 0x05: card->fifos[FIFO_B1][TX].iso_endpoint = ep; break;
		case 0x85: card->fifos[FIFO_B1][RX].iso_endpoint = ep; break;
		case 0x06: card->fifos[FIFO_B2][TX].iso_endpoint = ep; break;
		case 0x86: card->fifos[FIFO_B2][RX].iso_endpoint = ep; break;
		case 0x07: card->fifos[FIFO_D][TX].iso_endpoint = ep; break;
		case 0x87: card->fifos[FIFO_D][RX].iso_endpoint = ep; break;
		case 0x08: card->fifos[FIFO_PCME][TX].iso_endpoint = ep; break;
		case 0x88: card->fifos[FIFO_PCME][RX].iso_endpoint = ep; break;
		}
	}

	for (i=0; i<ARRAY_SIZE(card->fifos); i++) {
		if (!card->fifos[i][RX].int_endpoint) {
			printk(KERN_ERR
				"Unsupported USB configuration, missing"
				" endpoint for RX fifo %d\n", i);
			err = -EINVAL;
			goto err_invalid_config;
		}

		if (!card->fifos[i][TX].int_endpoint) {
			printk(KERN_ERR
				"Unsupported USB configuration, missing"
				" endpoint for TX fifo %d\n", i);
			err = -EINVAL;
			goto err_invalid_config;
		}
	}

	err = hfc_fifo_alloc(&card->fifos[FIFO_D][RX]);
	if (err < 0)
		goto err_fifo_alloc_d_rx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_D][TX]);
	if (err < 0)
		goto err_fifo_alloc_d_tx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_B1][RX]);
	if (err < 0)
		goto err_fifo_alloc_b1_rx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_B1][TX]);
	if (err < 0)
		goto err_fifo_alloc_b1_tx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_B2][RX]);
	if (err < 0)
		goto err_fifo_alloc_b2_rx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_B2][TX]);
	if (err < 0)
		goto err_fifo_alloc_b2_tx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_PCME][RX]);
	if (err < 0)
		goto err_fifo_alloc_pcme_rx;

	err = hfc_fifo_alloc(&card->fifos[FIFO_PCME][TX]);
	if (err < 0)
		goto err_fifo_alloc_pcme_tx;

	hfc_st_port_init(&card->st_port, card, "st0");

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		hfc_led_init(&card->leds[i], i, card);

	card->pipe_in = usb_rcvctrlpipe(card->usb_dev, 0);
	card->pipe_out = usb_sndctrlpipe(card->usb_dev, 0);

	INIT_WORK(&card->led_update_work, hfc_led_update_work, card);

	hfc_card_reset(card);

	{
	u8 chip_id;
	chip_id = hfc_read(card, HFC_REG_CHIP_ID) >> 4;
	hfc_msg(KERN_INFO, "Found card with chip_id=%02x\n", chip_id);
	}

	hfc_card_initialize(card);

	card->leds[HFC_LED_PC].color = HFC_LED_ON;

	hfc_led_update(&card->leds[HFC_LED_PC]);
	hfc_led_update(&card->leds[HFC_LED_B2]);
	hfc_led_update(&card->leds[HFC_LED_B1]);
	hfc_led_update(&card->leds[HFC_LED_ISDN]);
	hfc_led_update(&card->leds[HFC_LED_USB]);

	err = hfc_st_port_register(&card->st_port);
	if (err < 0)
		goto err_st_port_register;

	return 0;

	hfc_st_port_unregister(&card->st_port);
err_st_port_register:
	hfc_fifo_dealloc(&card->fifos[FIFO_PCME][TX]);
err_fifo_alloc_pcme_tx:
	hfc_fifo_dealloc(&card->fifos[FIFO_PCME][RX]);
err_fifo_alloc_pcme_rx:
	hfc_fifo_dealloc(&card->fifos[FIFO_B2][TX]);
err_fifo_alloc_b2_tx:
	hfc_fifo_dealloc(&card->fifos[FIFO_B2][RX]);
err_fifo_alloc_b2_rx:
	hfc_fifo_dealloc(&card->fifos[FIFO_B1][TX]);
err_fifo_alloc_b1_tx:
	hfc_fifo_dealloc(&card->fifos[FIFO_B1][RX]);
err_fifo_alloc_b1_rx:
	hfc_fifo_dealloc(&card->fifos[FIFO_D][TX]);
err_fifo_alloc_d_tx:
	hfc_fifo_dealloc(&card->fifos[FIFO_D][RX]);
err_fifo_alloc_d_rx:
err_invalid_config:
	kfree(card);
err_kmalloc:

	return err;
}

static void hfc_disconnect(struct usb_interface *usb_intf)
{
	struct hfc_card *card;
	int i;

	card = usb_get_intfdata(usb_intf);
	usb_set_intfdata(usb_intf, NULL);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d: "
		"shutting down.\n",
		card->id);

	for(i=0; i<ARRAY_SIZE(card->leds); i++)
		del_timer_sync(&card->leds[i].timer);

	hfc_card_reset(card);

	hfc_st_port_unregister(&card->st_port);

	hfc_fifo_dealloc(&card->fifos[FIFO_PCME][TX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_PCME][RX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_B2][TX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_B2][RX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_B1][TX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_B1][RX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_D][TX]);
	hfc_fifo_dealloc(&card->fifos[FIFO_D][RX]);

	kfree(card);
}

static struct usb_driver hfc_driver = {
	.owner		= THIS_MODULE,
	.name		= hfc_DRIVER_NAME,
	.probe		= hfc_probe,
	.disconnect	= hfc_disconnect,
	.id_table	= hfc_table,
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

static int __init hfc_init_module(void)
{
	int err;

	hfc_msg(KERN_INFO, hfc_DRIVER_DESCR " loading\n");

	err = usb_register(&hfc_driver);
	if (err)
		goto err_usb_register;

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
err_usb_register:

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

	usb_deregister(&hfc_driver);

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

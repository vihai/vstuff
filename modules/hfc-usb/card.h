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

#ifndef _HFC_CARD_H
#define _HFC_CARD_H

#include <linux/delay.h>
#include <linux/usb.h>

#include "led.h"
#include "st_port.h"

#ifdef DEBUG
#define hfc_debug_card(dbglevel, card, format, arg...)	\
	if (debug_level >= dbglevel)			\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX	\
			"card: %d "			\
			format, card->id, ## arg)
#else
#define hfc_debug_card(dbglevel, card, format, arg...) do {} while (0)
#endif

#define hfc_msg_card(level, card, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		format, card->id, ## arg)

#define hfc_CLKDEL_TE	0x0E
#define hfc_CLKDEL_NT	0x0C

struct hfc_card
{
	struct semaphore sem;

	int id;

	struct usb_device *usb_dev;
	struct usb_interface *usb_interface;

	int pipe_in;
	int pipe_out;

	struct hfc_st_port st_port;

	struct hfc_led leds[5];
	struct work_struct led_update_work;

	struct
	{
		u8 p_data;
	} regs;
};

enum hfc_leds
{
	HFC_LED_PC = 0,
	HFC_LED_B2 = 1,
	HFC_LED_B1 = 2,
	HFC_LED_ISDN = 3,
	HFC_LED_USB = 4,
};

#endif

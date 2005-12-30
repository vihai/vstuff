/*
 * Cologne Chip's HFC-USB vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_CARD_INLINE_H
#define _HFC_CARD_INLINE_H

#include "card.h"

static inline void hfc_write(struct hfc_card *card, u8 reg, u8 value)
{
//	printk(KERN_DEBUG hfc_DRIVER_PREFIX "REG %02x = %02x\n", reg, value);

	usb_control_msg(card->usb_dev, card->pipe_out,
		0, 0x40, value, reg, NULL, 0, USB_CTRL_SET_TIMEOUT * HZ);
}

static inline u8 hfc_read(struct hfc_card *card, u8 reg)
{
	u8 value;

	if (usb_control_msg(card->usb_dev, card->pipe_in, 1, 0xC0, 0, reg,
				&value, 1, USB_CTRL_GET_TIMEOUT * HZ) < 0)
		printk(KERN_ERR "Cannot read usb device\n");

	return value;
}

static inline void hfc_card_lock(struct hfc_card *card)
{
	down(&card->sem);
}

static inline void hfc_card_unlock(struct hfc_card *card)
{
	up(&card->sem);
}

#endif

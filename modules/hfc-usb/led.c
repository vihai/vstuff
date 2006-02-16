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

#include "card.h"
#include "card_inline.h"
#include "led.h"

void hfc_led_update(struct hfc_led *led)
{
	struct hfc_card *card = led->card;
	u8 new_leds = card->regs.p_data;
	enum hfc_led_color actual_color;

	actual_color = led->color;

	if (led->flashing_freq && led->flashes != 0) {

		mod_timer(&led->timer,
			jiffies + led->flashing_freq / 2);

	    	if ((jiffies % led->flashing_freq) >= (led->flashing_freq / 2))
			actual_color = HFC_LED_OFF;
	}

	if (actual_color == HFC_LED_ON) {
		switch(led->id) {
		case 0: new_leds &= ~0x08; break;
		case 1: new_leds &= ~0x10; break;
		case 2: new_leds &= ~0x20; break;
		case 3: new_leds &= ~0x40; break;
		case 4: new_leds |= 0x80; break;
		}
	} else {
		switch(led->id) {
		case 0: new_leds |= 0x08; break;
		case 1: new_leds |= 0x10; break;
		case 2: new_leds |= 0x20; break;
		case 3: new_leds |= 0x40; break;
		case 4: new_leds &= ~0x80; break;
		}
	}

	if (new_leds != card->regs.p_data) {
		card->regs.p_data = new_leds;
		schedule_work(&card->led_update_work);
	}
}

void hfc_led_update_work(void *data)
{
	struct hfc_card *card = data;

	hfc_write(card, HFC_REG_P_DATA, card->regs.p_data);
}

static void hfc_led_timer(unsigned long data)
{
	struct hfc_led *led = (struct hfc_led *)data;

	led->flashes--;
	hfc_led_update(led);
}

void hfc_led_init(
	struct hfc_led *led,
	int id,
	struct hfc_card *card)
{
	led->card = card;
	led->id = id;
	led->color = HFC_LED_OFF;
	led->flashing_freq = 0;
	led->flashes = 0;

	init_timer(&led->timer);
	led->timer.function = hfc_led_timer;
	led->timer.data = (unsigned long)led;
}

void hfc_led_remove(
	struct hfc_led *led)
{
	del_timer_sync(&led->timer);
}

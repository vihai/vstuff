/*
 * Cologne Chip's HFC-E1 vISDN driver
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
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>

#include "card.h"
#include "card_inline.h"
#include "led.h"

void hfc_led_update(struct hfc_led *led)
{
	struct hfc_card *card = led->card;
	enum hfc_led_color color;
	u8 out_bit = 0;

	if (led->flashing_freq &&
	    (jiffies % led->flashing_freq) > (led->flashing_freq / 2) &&
	    led->flashes != 0)
		color = led->alt_color;
	else
		color = led->color;

	switch(led->id) {
	case 0:
		out_bit = hfc_R_GPIO_OUT1_V_GPIO_OUT8;
	break;
	case 1:
		out_bit = hfc_R_GPIO_OUT1_V_GPIO_OUT9;
	break;
	case 2:
		out_bit = hfc_R_GPIO_OUT1_V_GPIO_OUT10;
	break;
	case 3:
		out_bit = hfc_R_GPIO_OUT1_V_GPIO_OUT11;
	break;
	}

	if (color != HFC_LED_OFF)
		card->gpio_out &= ~out_bit;
	else
		card->gpio_out |= out_bit;

	hfc_outb(card, hfc_R_GPIO_OUT1, card->gpio_out);

	if (led->flashing_freq && led->flashes != 0) {
		led->timer.expires = jiffies + led->flashing_freq / 2;
		add_timer(&led->timer);
	}
}

static void hfc_led_timer(unsigned long data)
{
	struct hfc_led *led = (struct hfc_led *)data;
	struct hfc_card *card = led->card;

	hfc_card_lock(card);
	led->flashes--;
	hfc_led_update(led);
	hfc_card_unlock(card);
}

void hfc_led_init(
	struct hfc_led *led,
	int id,
	struct hfc_card *card)
{
	led->card = card;
	led->id = id;
	led->color = HFC_LED_OFF;
	led->alt_color = HFC_LED_OFF;
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

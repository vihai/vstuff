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

#ifndef _HFC_LED_H
#define _HFC_LED_H

enum hfc_led_color
{
	HFC_LED_OFF,
	HFC_LED_RED,
	HFC_LED_GREEN,
};

struct hfc_led
{
	struct hfc_card *card;

	int id;

	enum hfc_led_color color;
	enum hfc_led_color alt_color;
	int flashing_freq;
	int flashes;

	struct timer_list timer;
};

extern void hfc_update_led(struct hfc_led *led);

extern void hfc_led_init(
	struct hfc_led *led,
	int id,
	struct hfc_card *card);

extern void hfc_led_remove(struct hfc_led *led);

#endif

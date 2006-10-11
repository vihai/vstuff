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

#ifndef _HFC_SWITCH_H
#define _HFC_SWITCH_H

#define to_hfc_switch(s)	\
		container_of(s, struct hfc_switch, ks_node)

struct hfc_switch
{
	struct ks_node ks_node;

	struct hfc_card *card;
};

void hfc_switch_init(
	struct hfc_switch *hfcswitch,
	struct hfc_card *card);

int hfc_switch_register(struct hfc_switch *hfcswitch);
void hfc_switch_unregister(struct hfc_switch *hfcswitch);

#endif

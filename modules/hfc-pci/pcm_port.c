/*
 * Cologne Chip's HFC-S PCI A vISDN driver
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

#include "st_port.h"
#include "card.h"
#include "card_inline.h"

//----------------------------------------------------------------------------

static ssize_t hfc_show_bitrate(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 2);
}

static VISDN_PORT_ATTR(bitrate, S_IRUGO,
		hfc_show_bitrate,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_master(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->master ? 1 : 0);
}

static ssize_t hfc_store_master(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	if (value != 0 && value != 1)
		return -EINVAL;

	hfc_card_lock(card);
	port->master = value;
//	hfc_update_pcm_md0(card, 0);
	// FIXME TODO
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(master, S_IRUGO | S_IWUSR,
		hfc_show_master,
		hfc_store_master);

//----------------------------------------------------------------------------

static ssize_t hfc_show_slots_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
		"Slot    Chan\n");

	hfc_card_lock(card);

#if 0
	int i;
	for (i=0; i<port->num_slots; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
			"[%2d,%s]",
			port->slots[i][RX].hw_index,
			port->slots[i][RX].direction == RX ? "RX" : "TX");

		if (port->slots[i][RX].connected_chan) {
			len += snprintf(buf + len, PAGE_SIZE - len,
				" [%2d,%s]",
				port->slots[i][RX].connected_chan->chan->hw_index,
				port->slots[i][RX].connected_chan->direction == RX ?
					"RX" : "TX");
		}

		len += snprintf(buf + len, PAGE_SIZE - len, "\n");

		len += snprintf(buf + len, PAGE_SIZE - len,
			"[%2d,%s]",
			port->slots[i][TX].hw_index,
			port->slots[i][TX].direction == RX ? "RX" : "TX");

		if (port->slots[i][TX].connected_chan) {
			len += snprintf(buf + len, PAGE_SIZE - len,
				" [%2d,%s]",
				port->slots[i][TX].connected_chan->chan->hw_index,
				port->slots[i][TX].connected_chan->direction == RX ?
					"RX" : "TX");
		}

		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
#endif

	hfc_card_unlock(card);

	return len;

}

static VISDN_PORT_ATTR(slots_state, S_IRUGO,
		hfc_show_slots_state,
		NULL);

int hfc_pcm_port_sysfs_create_files(
	struct hfc_pcm_port *port)
{
	int err;

	err = visdn_port_create_file(
		&port->visdn_port,
		&visdn_port_attr_master);
	if (err < 0)
		goto err_create_file_master;

	err = visdn_port_create_file(
		&port->visdn_port,
		&visdn_port_attr_bitrate);
	if (err < 0)
		goto err_create_file_bitrate;

	err = visdn_port_create_file(
		&port->visdn_port,
		&visdn_port_attr_slots_state);
	if (err < 0)
		goto err_create_file_slots_state;

	return 0;

	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_slots_state);
err_create_file_slots_state:
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_bitrate);
err_create_file_bitrate:
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_master);
err_create_file_master:

	return err;

	return 0;
}

void hfc_pcm_port_sysfs_delete_files(
	struct hfc_pcm_port *port)
{
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_slots_state);
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_bitrate);
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_master);
}

struct hfc_pcm_slot *hfc_pcm_port_allocate_slot(
	struct hfc_pcm_port *port,
	enum hfc_direction direction)
{
	int i;
	for (i=0; i<port->num_slots; i++) {
		if (!port->slots[i][direction].used) {
			port->slots[i][direction].used = TRUE;
			return &port->slots[i][direction];
		}
	}

	return NULL;
}

void hfc_pcm_port_deallocate_slot(struct hfc_pcm_slot *slot)
{
	slot->used = FALSE;
}

static void hfc_pcm_port_release(struct visdn_port *port)
{
	printk(KERN_DEBUG "hfc_pcm_port_release()\n");

	// FIXME
}

static int hfc_pcm_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_card_unlock(card);

	hfc_debug_pcm_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_pcm_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_card_unlock(card);

	hfc_debug_pcm_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_pcm_port_ops = {
	.owner		= THIS_MODULE,
	.release	= hfc_pcm_port_release,
	.enable		= hfc_pcm_port_enable,
	.disable	= hfc_pcm_port_disable,
};

static inline void hfc_pcm_port_slot_init(
	struct hfc_pcm_slot *slot,
	struct hfc_pcm_port *port,
	int hw_index,
	enum hfc_direction direction)
{
	slot->port = port;
	slot->hw_index = hw_index;
	slot->direction = direction;
}

void hfc_pcm_port_init(
	struct hfc_pcm_port *port,
	struct hfc_card *card,
	const char *name)
{
	int i;

	port->card = card;
	visdn_port_init(&port->visdn_port);
	port->visdn_port.ops = &hfc_pcm_port_ops;
	port->visdn_port.device = &card->pci_dev->dev;
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));;

	for (i=0; i<sizeof(port->slots)/sizeof(*port->slots); i++) {
		hfc_pcm_port_slot_init(&port->slots[i][RX], port, i, RX);
		hfc_pcm_port_slot_init(&port->slots[i][TX], port, i, TX);
	}
}

int hfc_pcm_port_register(struct hfc_pcm_port *port)
{
	int err;
//	int i;

	err = visdn_port_register(&port->visdn_port);
	if (err < 0)
		goto err_port_register;

/*
	for (i=0; i<port->num_chans; i++) {
		err = hfc_pcm_chan_register(&port->chans[i]);
		if (err < 0)
			goto err_chan_register;
	}*/

	hfc_pcm_port_sysfs_create_files(port);

	return 0;

/*err_chan_register:
	visdn_port_unregister(&port->visdn_port);*/
err_port_register:

	return err;
}

void hfc_pcm_port_unregister(struct hfc_pcm_port *port)
{
//	int i;

	hfc_pcm_port_sysfs_delete_files(port);

/*	for (i=0; i<port->num_chans; i++) {
		hfc_pcm_chan_unregister(&port->chans[i]);
	}*/

	visdn_port_unregister(&port->visdn_port);
}

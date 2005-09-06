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

#include <kernel_config.h>

#include "pcm_port.h"
#include "pcm_port_sysfs.h"
#include "card.h"
#include "card_inline.h"

static ssize_t hfc_show_bitrate(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 2);
}

static DEVICE_ATTR(bitrate, S_IRUGO,
		hfc_show_bitrate,
		NULL);

//----------------------------------------------------------------------------

static ssize_t hfc_show_master(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", port->master ? 1 : 0);
}

static ssize_t hfc_store_master(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	if (value != 0 && value != 1)
		return -EINVAL;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;
	port->master = value;
//	hfc_update_pcm_md0(card, 0);
	// FIXME TODO
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(master, S_IRUGO | S_IWUSR,
		hfc_show_master,
		hfc_store_master);

//----------------------------------------------------------------------------

static ssize_t hfc_show_slots_state(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
		"Slot    Chan\n");

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;

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

	hfc_card_unlock(card);

	return len;

}

static DEVICE_ATTR(slots_state, S_IRUGO,
		hfc_show_slots_state,
		NULL);

int hfc_pcm_port_sysfs_create_files(
        struct hfc_pcm_port *port)
{
	int err;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_master);
	if (err < 0)
		goto err_device_create_file_master;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_bitrate);
	if (err < 0)
		goto err_device_create_file_bitrate;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_slots_state);
	if (err < 0)
		goto err_device_create_file_slots_state;

	return 0;

	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_slots_state);
err_device_create_file_slots_state:
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_bitrate);
err_device_create_file_bitrate:
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_master);
err_device_create_file_master:

	return err;

	return 0;
}

void hfc_pcm_port_sysfs_delete_files(
        struct hfc_pcm_port *port)
{
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_slots_state);
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_bitrate);
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_master);
}

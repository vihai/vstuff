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

#include <kernel_config.h>

#include "pcm_port.h"
#include "pcm_port_sysfs.h"
#include "card.h"
#include "card_inline.h"

static ssize_t hfc_show_bitrate(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);

	int bitrate = 0;

	switch(port->bitrate) {
	case 0: bitrate = 2; break;
	case 1: bitrate = 4; break;
	case 2: bitrate = 8; break;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", bitrate);

}

static ssize_t hfc_store_bitrate(
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

	if (value == 2)
		port->bitrate = 0;
	else if (value == 4)
		port->bitrate = 1;
	else if (value == 8)
		port->bitrate = 2;
	return -EINVAL;

	if (hfc_card_lock_interruptible(port->card))
		return -ERESTARTSYS;
	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_PCM_RES);
	mb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();
	hfc_wait_busy(card);
	hfc_update_pcm_md0(card, 0);
	hfc_update_pcm_md1(card);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(bitrate, S_IRUGO | S_IWUSR,
		hfc_show_bitrate,
		hfc_store_bitrate);

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

	if (hfc_card_lock_interruptible(port->card))
		return -ERESTARTSYS;
	port->master = value;
	hfc_update_pcm_md0(card, 0);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(master, S_IRUGO | S_IWUSR,
		hfc_show_master,
		hfc_store_master);

//----------------------------------------------------------------------------


static ssize_t hfc_show_f0io_counter(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	if (hfc_card_lock_interruptible(port->card))
		return -ERESTARTSYS;
	u16 counter;

	counter = hfc_inb(card, hfc_R_F0_CNTL);
	counter += hfc_inb(card, hfc_R_F0_CNTH) << 8;

	hfc_card_unlock(card);

	return snprintf(buf, PAGE_SIZE, "%d\n", counter);

}

static VISDN_PORT_ATTR(f0io_counter, S_IRUGO,
		hfc_show_f0io_counter,
		NULL);

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

	if (hfc_card_lock_interruptible(port->card))
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
		&visdn_port_attr_f0io_counter);
	if (err < 0)
		goto err_create_file_f0io_counter;

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
		&visdn_port_attr_f0io_counter);
err_create_file_f0io_counter:
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
		&visdn_port_attr_f0io_counter);
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_bitrate);
	visdn_port_remove_file(
		&port->visdn_port,
		&visdn_port_attr_master);
}

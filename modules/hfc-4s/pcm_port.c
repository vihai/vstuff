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

#include "pcm_port.h"
#include "pcm_chan.h"
#include "card.h"
#include "card_inline.h"

//----------------------------------------------------------------------------

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
	int i;

	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	if (value == 2)
		port->bitrate = 0;
	else if (value == 4)
		port->bitrate = 1;
	else if (value == 8)
		port->bitrate = 2;
	else
		return -EINVAL;

	for (i=0; i<port->num_chans; i++) {
		hfc_pcm_chan_unregister(&port->chans[i]);
	}

	hfc_card_lock(card);
	hfc_outb(card, hfc_R_CIRM, hfc_R_CIRM_V_PCM_RES);
	mb();
	hfc_outb(card, hfc_R_CIRM, 0);
	mb();
	hfc_wait_busy(card);
	hfc_update_pcm_md0(card, 0);
	hfc_update_pcm_md1(card);

// Temporary workaround for too big kmalloc
	hfc_pcm_port_configure(port, 0);

/*	switch(port->bitrate) {
	case 0: hfc_pcm_port_configure(port, 32); break;
	case 1: hfc_pcm_port_configure(port, 64); break;
	case 2: hfc_pcm_port_configure(port, 128); break;
	}*/

	hfc_card_unlock(card);

	for (i=0; i<port->num_chans; i++) {
		hfc_pcm_chan_register(&port->chans[i]);
	}

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

	hfc_card_lock(card);
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
	u16 counter;

	hfc_card_lock(card);

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
/*
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
		"Slot    Chan\n");

	hfc_card_lock(card);

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
*/

	return 0;
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

void hfc_pcm_port_configure(struct hfc_pcm_port *port, int slots)
{
	port->num_chans = slots;

/*
	for (i=0; i<port->num_chans; i++) {
		hfc_pcm_port_configure_slot(&port->chans[i].rx_slot);
		hfc_pcm_port_configure_slot(&port->chans[i].tx_slot);
	}*/

	
}

static void hfc_pcm_port_release(
	struct visdn_port *visdn_port)
{
	printk(KERN_DEBUG "hfc_pcm_port_release()\n");

	// FIXME
}

struct visdn_port_ops hfc_pcm_port_ops = {
	.owner		= THIS_MODULE,
	.release	= hfc_pcm_port_release,
};

static inline void hfc_pcm_port_slot_init(
	struct hfc_pcm_slot *slot,
	struct hfc_pcm_chan *chan,
	int hw_index,
	enum hfc_direction direction)
{
	slot->chan = chan;
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
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));

	for (i=0; i<ARRAY_SIZE(port->chans); i++) {
		char chan_name[16];

		snprintf(chan_name, sizeof(chan_name), "pcm%d", i);

		hfc_pcm_chan_init(&port->chans[i], port, chan_name, i, i);
	}

	port->num_chans = 0;
}

int hfc_pcm_port_register(
	struct hfc_pcm_port *port)
{
	int err;
	int i;

	err = visdn_port_register(&port->visdn_port);
	if (err < 0)
		goto err_port_register;

printk(KERN_DEBUG "NUM CHANS: %d\n", port->num_chans);
	port->num_chans = 0;

	for (i=0; i<port->num_chans; i++) {
		err = hfc_pcm_chan_register(&port->chans[i]);
		if (err < 0)
			goto err_chan_register;
	}

	hfc_pcm_port_sysfs_create_files(port);

	return 0;

err_chan_register:
	visdn_port_unregister(&port->visdn_port);
err_port_register:

	return err;
}

void hfc_pcm_port_unregister(
	struct hfc_pcm_port *port)
{
	int i;

	hfc_pcm_port_sysfs_delete_files(port);

	for (i=0; i<port->num_chans; i++) {
		hfc_pcm_chan_unregister(&port->chans[i]);
	}

	visdn_port_unregister(&port->visdn_port);
}

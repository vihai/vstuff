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

#include "chan.h"
#include "chan_sysfs.h"
#include "card.h"
#include "fifo_inline.h"

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	int bits = hfc_SQ_REC_BITS(hfc_inb(card, hfc_SQ_REC));

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	hfc_outb(card, hfc_SQ_SEND, hfc_SQ_SEND_BITS(value));

	return count;
}

static DEVICE_ATTR(sq_bits, S_IRUGO | S_IWUSR,
		hfc_show_sq_bits,
		hfc_store_sq_bits);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_enabled(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->sq_enabled ? 1 : 0));

}

static ssize_t hfc_store_sq_enabled(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	if (hfc_card_lock_interruptible(card))
		return -ERESTARTSYS;
	port->sq_enabled = !!value;
	hfc_st_port_update_sctrl(port),
	hfc_card_unlock(card);

	return count;
}

static DEVICE_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);


static int hfc_chan_sysfs_create_files_DB(
	struct hfc_chan_duplex *chan)
{
	return 0;
}

int hfc_chan_sysfs_create_files_D(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_B(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_SQ(
	struct hfc_chan_duplex *chan)
{
	int err;

	err = device_create_file(
		&chan->visdn_chan.device,
		&dev_attr_sq_bits);
	if (err < 0)
		goto err_device_create_file_sq_bits;

	err = device_create_file(
		&chan->visdn_chan.device,
		&dev_attr_sq_enabled);
	if (err < 0)
		goto err_device_create_file_sq_enabled;

	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_enabled);
err_device_create_file_sq_enabled:
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_bits);
err_device_create_file_sq_bits:

	return err;
}

void hfc_chan_sysfs_delete_files_DB(
	struct hfc_chan_duplex *chan)
{
}

void hfc_chan_sysfs_delete_files_D(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_B(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_SQ(
	struct hfc_chan_duplex *chan)
{
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_enabled);
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_bits);
}


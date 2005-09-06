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
#include <linux/spinlock.h>

#include "card.h"
#include "card_inline.h"
#include "fifo.h"
#include "fifo_inline.h"

//----------------------------------------------------------------------------

static ssize_t hfc_show_fifo_state(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct hfc_card *card = pci_get_drvdata(pci_dev);

	int len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
		"       Receive                Transmit\n"
		"Fifo#  F1 F2   Z1   Z2 Used   F1 F2   Z1   Z2 Used Connected\n");
	int i;
	for (i=0; i<3; i++) {
		struct hfc_fifo *fifo_rx = &card->fifos[i][RX];
		struct hfc_fifo *fifo_tx = &card->fifos[i][TX];

		len += snprintf(buf + len, PAGE_SIZE - len,
			"%2d     %02x %02x %04x %04x %4d   %02x %02x %04x %04x %4d",
			i,
			*fifo_rx->f1,
			*fifo_rx->f2,
			*Z1_F2(fifo_rx),
			*Z2_F2(fifo_rx),
			hfc_fifo_used_rx(fifo_rx),
			*fifo_tx->f1,
			*fifo_tx->f2,
			*Z1_F1(fifo_tx),
			*Z2_F1(fifo_tx),
			hfc_fifo_used_tx(fifo_tx));

		if (fifo_rx->connected_chan) {
			len += snprintf(buf + len, PAGE_SIZE - len,
				" st:%s",
				fifo_rx->connected_chan->chan->name);
		}

		if (fifo_tx->connected_chan) {
			len += snprintf(buf + len, PAGE_SIZE - len,
				" st:%s",
				fifo_tx->connected_chan->chan->name);
		}

		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}

	return len;
}

static DEVICE_ATTR(fifo_state, S_IRUGO,
		hfc_show_fifo_state,
		NULL);


int hfc_card_sysfs_create_files(
	struct hfc_card *card)
{
	int err;

	err = device_create_file(
		&card->pcidev->dev,
		&dev_attr_fifo_state);
	if (err < 0)
		goto err_device_create_file_fifo_state;

	return 0;

	device_remove_file(&card->pcidev->dev, &dev_attr_fifo_state);
err_device_create_file_fifo_state:

	return err;
}

void hfc_card_sysfs_delete_files(
	struct hfc_card *card)
{
	device_remove_file(&card->pcidev->dev, &dev_attr_fifo_state);
}
